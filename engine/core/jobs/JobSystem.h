#pragma once

#include "engine/core/Log.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <thread>

namespace engine::core::jobs
{
	/// Type-erased callable stored inline (no allocation).
	class JobFn final
	{
	public:
		JobFn() = default;
		JobFn(const JobFn&) = delete;
		JobFn& operator=(const JobFn&) = delete;

		JobFn(JobFn&& other) noexcept
			: m_invoke(other.m_invoke)
			, m_destroy(other.m_destroy)
		{
			for (size_t i = 0; i < kStorageSize; ++i)
			{
				m_storage[i] = other.m_storage[i];
			}
			other.m_invoke = nullptr;
			other.m_destroy = nullptr;
		}

		JobFn& operator=(JobFn&& other) noexcept
		{
			if (this == &other)
			{
				return *this;
			}
			Reset();
			m_invoke = other.m_invoke;
			m_destroy = other.m_destroy;
			for (size_t i = 0; i < kStorageSize; ++i)
			{
				m_storage[i] = other.m_storage[i];
			}
			other.m_invoke = nullptr;
			other.m_destroy = nullptr;
			return *this;
		}

		~JobFn()
		{
			Reset();
		}

		template <class F>
		static JobFn Make(F&& fn)
		{
			using T = std::decay_t<F>;
			static_assert(sizeof(T) <= kStorageSize, "JobFn capture too large (avoid per-job allocations)");
			static_assert(alignof(T) <= alignof(std::max_align_t), "JobFn capture over-aligned");
			static_assert(std::is_trivially_copyable_v<T>, "JobFn requires trivially copyable callables (keep captures simple)");

			JobFn out;
			new (out.m_storage) T(std::forward<F>(fn));
			out.m_invoke = [](void* p)
			{
				(*reinterpret_cast<T*>(p))();
			};
			out.m_destroy = [](void* p)
			{
				reinterpret_cast<T*>(p)->~T();
			};
			return out;
		}

		void Invoke()
		{
			if (m_invoke)
			{
				m_invoke(m_storage);
			}
		}

		bool Valid() const { return m_invoke != nullptr; }

	private:
		void Reset()
		{
			if (m_destroy)
			{
				m_destroy(m_storage);
			}
			m_invoke = nullptr;
			m_destroy = nullptr;
		}

		static constexpr size_t kStorageSize = 64;
		alignas(std::max_align_t) std::byte m_storage[kStorageSize]{};
		void (*m_invoke)(void*) = nullptr;
		void (*m_destroy)(void*) = nullptr;
	};

	/// Group of jobs that can be waited on.
	class JobGroup final
	{
	public:
		JobGroup() = default;
		JobGroup(const JobGroup&) = delete;
		JobGroup& operator=(const JobGroup&) = delete;

		/// Returns true when all jobs in the group have completed.
		bool IsDone() const { return m_count.load(std::memory_order_acquire) == 0; }

	private:
		friend struct Jobs;
		std::atomic<uint32_t> m_count{ 0 };
	};

	/// Handle for a single job (implemented as an internal group of size 1).
	class JobHandle final
	{
	public:
		JobHandle() = default;
		JobHandle(const JobHandle&) = delete;
		JobHandle& operator=(const JobHandle&) = delete;
		JobHandle(JobHandle&&) = default;
		JobHandle& operator=(JobHandle&&) = default;

		/// Returns true when the job has completed.
		bool IsDone() const { return m_group.IsDone(); }

	private:
		friend struct Jobs;
		JobGroup m_group{};
	};

	struct Jobs final
	{
		/// Initialize the job system and spawn worker threads.
		/// If `workerCount` is 0, defaults to `max(1, hardware_concurrency - 1)`.
		static void Init(uint32_t workerCount = 0);

		/// Shutdown the job system and join all worker threads. Safe to call multiple times.
		static void Shutdown();

		/// Enqueue a single job and return a handle to wait on.
		template <class F>
		static JobHandle Enqueue(F&& fn)
		{
			JobHandle h;
			Enqueue(h.m_group, std::forward<F>(fn));
			return h;
		}

		/// Enqueue a job into a group (the group must outlive the job execution).
		template <class F>
		static void Enqueue(JobGroup& group, F&& fn)
		{
			group.m_count.fetch_add(1, std::memory_order_release);
			EnqueueInternal(&group, JobFn::Make(std::forward<F>(fn)));
		}

		/// Wait for a job handle (pumps jobs to avoid deadlock even from a worker thread).
		static void Wait(JobHandle& handle);

		/// Wait for a job group (pumps jobs to avoid deadlock even from a worker thread).
		static void Wait(JobGroup& group);

	private:
		static void EnqueueInternal(JobGroup* group, JobFn&& fn);
	};
}

