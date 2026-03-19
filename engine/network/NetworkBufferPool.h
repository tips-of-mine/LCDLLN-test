#pragma once

// M25.3 — Buffer pooling for hot-path network RX/TX.
//
// Fixed buffer capacities:
// - small: 4 KB
// - large: 16 KB (aligned with protocol v1 max packet size)
//
// Thread-local caches reduce lock contention; a sharded global free-list handles cross-thread reuse.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "engine/core/Log.h"

namespace engine::network
{
	class NetworkBufferPool final
	{
	public:
		static constexpr size_t kSmallCap = 4u * 1024u;
		static constexpr size_t kLargeCap = 16u * 1024u;
		static constexpr size_t kBuckets = 2u;

		// Move-only owning view of a pooled buffer.
		class PooledBuffer
		{
		public:
			PooledBuffer() = default;
			PooledBuffer(NetworkBufferPool* pool, uint8_t* ptr, size_t cap, size_t actual, uint32_t bucket) noexcept
				: m_pool(pool), m_ptr(ptr), m_cap(cap), m_actual(actual), m_bucket(bucket)
			{}

			PooledBuffer(const PooledBuffer&) = delete;
			PooledBuffer& operator=(const PooledBuffer&) = delete;

			PooledBuffer(PooledBuffer&& other) noexcept
				: m_pool(other.m_pool), m_ptr(other.m_ptr), m_cap(other.m_cap), m_actual(other.m_actual), m_bucket(other.m_bucket)
			{
				other.m_pool = nullptr;
				other.m_ptr = nullptr;
				other.m_cap = 0;
				other.m_actual = 0;
				other.m_bucket = 0;
			}

			PooledBuffer& operator=(PooledBuffer&& other) noexcept
			{
				if (this == &other)
					return *this;
				Release();
				m_pool = other.m_pool;
				m_ptr = other.m_ptr;
				m_cap = other.m_cap;
				m_actual = other.m_actual;
				m_bucket = other.m_bucket;
				other.m_pool = nullptr;
				other.m_ptr = nullptr;
				other.m_cap = 0;
				other.m_actual = 0;
				other.m_bucket = 0;
				return *this;
			}

			~PooledBuffer() { Release(); }

			uint8_t* data() noexcept { return m_ptr; }
			const uint8_t* data() const noexcept { return m_ptr; }
			size_t size() const noexcept { return m_actual; }
			size_t capacity() const noexcept { return m_cap; }

			explicit operator bool() const noexcept { return m_ptr != nullptr && m_actual > 0; }

		private:
			void Release() noexcept
			{
				if (m_pool != nullptr && m_ptr != nullptr)
				{
					m_pool->Release(m_bucket, m_ptr);
					m_pool = nullptr;
					m_ptr = nullptr;
					m_cap = 0;
					m_actual = 0;
					m_bucket = 0;
				}
			}

			NetworkBufferPool* m_pool = nullptr;
			uint8_t*           m_ptr = nullptr;
			size_t             m_cap = 0;
			size_t             m_actual = 0;
			uint32_t           m_bucket = 0;
		};

		struct Metrics
		{
			uint64_t acquires = 0;
			uint64_t reuseHits = 0;
			uint64_t newAllocs = 0;
			uint64_t releases = 0;
		};

		static bool Init()
		{
			auto& pool = Get();
			bool expected = false;
			if (!pool.m_initialized.compare_exchange_strong(expected, true))
			{
				LOG_INFO(Net, "[NetworkBufferPool] Init OK (already initialized)");
				return true;
			}

			LOG_INFO(Net, "[NetworkBufferPool] Init OK (caps={}/{} buckets={})",
				static_cast<uint64_t>(kSmallCap), static_cast<uint64_t>(kLargeCap), static_cast<uint64_t>(kBuckets));
			return true;
		}

		/// Convenience static access: performs Init() lazily.
		static PooledBuffer AcquireBuffer(size_t actualSize)
		{
			(void)Init();
			return Get().Acquire(actualSize);
		}

		static Metrics ConsumeMetrics()
		{
			// Metrics are only meaningful after some traffic; still safe to consume anytime.
			return Get().GetAndResetMetrics();
		}

		// Acquire a buffer with fixed capacity class; actual size can be smaller.
		PooledBuffer Acquire(size_t actualSize)
		{
			if (actualSize == 0)
				return {};

			m_acquires.fetch_add(1, std::memory_order_relaxed);

			const uint32_t bucket = (actualSize <= kSmallCap) ? 0u : 1u;
			const size_t cap = (bucket == 0u) ? kSmallCap : kLargeCap;

			// Thread-local fast-path (no locks).
			std::vector<uint8_t*>& local = GetLocal(bucket);
			if (!local.empty())
			{
				uint8_t* ptr = local.back();
				local.pop_back();
				m_reuseHits.fetch_add(1, std::memory_order_relaxed);
				return PooledBuffer(this, ptr, cap, actualSize, bucket);
			}

			// Sharded global free-list (limits lock contention across many worker threads).
			const size_t shardIndex = ShardIndexForCurrentThread();
			Shard& shard = m_shards[shardIndex];
			{
				std::lock_guard lock(shard.mtx);
				std::vector<uint8_t*>& freeList = (bucket == 0u) ? shard.smallFree : shard.largeFree;
				if (!freeList.empty())
				{
					uint8_t* ptr = freeList.back();
					freeList.pop_back();
					m_reuseHits.fetch_add(1, std::memory_order_relaxed);
					return PooledBuffer(this, ptr, cap, actualSize, bucket);
				}
			}

			// Allocate new buffer.
			uint8_t* ptr = new (std::nothrow) uint8_t[cap];
			if (ptr == nullptr)
			{
				LOG_ERROR(Net, "[NetworkBufferPool] Acquire FAILED: allocation cap={} bytes", static_cast<uint64_t>(cap));
				return {};
			}
			{
				const uint64_t prev = m_newAllocs.fetch_add(1, std::memory_order_relaxed);
				if (prev == 0)
					LOG_INFO(Net, "[NetworkBufferPool] First new buffer allocation (cap={} bytes)", static_cast<uint64_t>(cap));
			}
			return PooledBuffer(this, ptr, cap, actualSize, bucket);
		}

		Metrics GetAndResetMetrics()
		{
			Metrics m;
			m.acquires = m_acquires.exchange(0, std::memory_order_relaxed);
			m.reuseHits = m_reuseHits.exchange(0, std::memory_order_relaxed);
			m.newAllocs = m_newAllocs.exchange(0, std::memory_order_relaxed);
			m.releases = m_releases.exchange(0, std::memory_order_relaxed);
			return m;
		}

	private:
		struct Shard
		{
			std::mutex mtx;
			std::vector<uint8_t*> smallFree;
			std::vector<uint8_t*> largeFree;
		};

		static NetworkBufferPool& Get()
		{
			static NetworkBufferPool s_pool;
			return s_pool;
		}

		static size_t ShardIndexForThread()
		{
			const size_t tidHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
			return tidHash % kShardCount;
		}

		size_t ShardIndexForCurrentThread() const { return ShardIndexForThread(); }

		std::vector<uint8_t*>& GetLocal(uint32_t bucket)
		{
			if (bucket == 0u)
				return LocalSmall();
			return LocalLarge();
		}

		static std::vector<uint8_t*>& LocalSmall()
		{
			thread_local std::vector<uint8_t*> s;
			return s;
		}

		static std::vector<uint8_t*>& LocalLarge()
		{
			thread_local std::vector<uint8_t*> s;
			return s;
		}

		void Release(uint32_t bucket, uint8_t* ptr) noexcept
		{
			if (ptr == nullptr)
				return;

			// Return to thread-local stash (fast path).
			std::vector<uint8_t*>& local = GetLocal(bucket);
			constexpr size_t kLocalMax = 32u;
			if (local.size() < kLocalMax)
			{
				local.push_back(ptr);
				m_releases.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			// Otherwise push back to the sharded global free-list.
			const size_t shardIndex = ShardIndexForCurrentThread();
			Shard& shard = m_shards[shardIndex];
			std::lock_guard lock(shard.mtx);
			if (bucket == 0u)
				shard.smallFree.push_back(ptr);
			else
				shard.largeFree.push_back(ptr);

			m_releases.fetch_add(1, std::memory_order_relaxed);
		}

	private:
		NetworkBufferPool() = default;

		static constexpr size_t kShardCount = 8u;
		std::array<Shard, kShardCount> m_shards{};

		std::atomic<bool> m_initialized{ false };
		std::atomic<uint64_t> m_acquires{ 0 };
		std::atomic<uint64_t> m_reuseHits{ 0 };
		std::atomic<uint64_t> m_newAllocs{ 0 };
		std::atomic<uint64_t> m_releases{ 0 };
	};
}

