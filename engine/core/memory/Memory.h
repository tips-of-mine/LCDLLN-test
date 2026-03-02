#pragma once

#include "engine/core/Log.h"
#include "engine/core/memory/MemoryTags.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <new>
#include <string_view>

namespace engine::core::memory
{
	struct MemTagStats
	{
		std::atomic<uint64_t> currentBytes{ 0 };
		std::atomic<uint64_t> peakBytes{ 0 };
		std::atomic<uint64_t> allocCount{ 0 };
		std::atomic<uint64_t> freeCount{ 0 };
	};

	/// Return a human-readable tag name (stable, for logs).
	constexpr std::string_view ToString(MemTag tag)
	{
		switch (tag)
		{
		case MemTag::Core: return "Core";
		case MemTag::Render: return "Render";
		case MemTag::Assets: return "Assets";
		case MemTag::World: return "World";
		case MemTag::Net: return "Net";
		case MemTag::UI: return "UI";
		case MemTag::Tools: return "Tools";
		case MemTag::Temp: return "Temp";
		default: return "Unknown";
		}
	}

	namespace detail
	{
		inline std::array<MemTagStats, static_cast<size_t>(MemTag::_Count)> g_stats{};

		inline void TrackAlloc(MemTag tag, uint64_t bytes)
		{
			auto& s = g_stats[static_cast<size_t>(tag)];
			const uint64_t cur = s.currentBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
			s.allocCount.fetch_add(1, std::memory_order_relaxed);

			uint64_t peak = s.peakBytes.load(std::memory_order_relaxed);
			while (cur > peak && !s.peakBytes.compare_exchange_weak(peak, cur, std::memory_order_relaxed))
			{
				// retry
			}
		}

		inline void TrackFree(MemTag tag, uint64_t bytes, uint64_t frees)
		{
			auto& s = g_stats[static_cast<size_t>(tag)];
			s.currentBytes.fetch_sub(bytes, std::memory_order_relaxed);
			s.freeCount.fetch_add(frees, std::memory_order_relaxed);
		}
	}

	/// Generic allocator API: `alloc(size, align, tag)` / `free(ptr, tag)`.
	struct SystemAllocator final
	{
		/// Allocate aligned memory. Tracking is updated per tag.
		static void* alloc(size_t size, size_t align, MemTag tag)
		{
			struct Header
			{
				void* base = nullptr;
				uint64_t size = 0;
				MemTag tag = MemTag::Core;
				uint8_t pad[7]{};
			};

			size_t a = (align == 0) ? alignof(std::max_align_t) : align;
			a = (a < alignof(Header)) ? alignof(Header) : a;
			a = std::bit_ceil(a); // ensure power-of-two for bitmask alignment

			const size_t total = sizeof(Header) + size + a;
			void* base = ::operator new(total);

			const uintptr_t start = reinterpret_cast<uintptr_t>(base) + sizeof(Header);
			const uintptr_t aligned = (start + (a - 1)) & ~(static_cast<uintptr_t>(a) - 1);
			auto* h = reinterpret_cast<Header*>(aligned - sizeof(Header));
			h->base = base;
			h->size = static_cast<uint64_t>(size);
			h->tag = tag;

			detail::TrackAlloc(tag, static_cast<uint64_t>(size));
			return reinterpret_cast<void*>(aligned);
		}

		/// Free aligned memory previously returned by `alloc`. Tracking is updated per tag.
		static void free(void* ptr, MemTag tag)
		{
			if (!ptr)
			{
				return;
			}

			struct Header
			{
				void* base = nullptr;
				uint64_t size = 0;
				MemTag tag = MemTag::Core;
				uint8_t pad[7]{};
			};

			const auto aligned = reinterpret_cast<uintptr_t>(ptr);
			auto* h = reinterpret_cast<Header*>(aligned - sizeof(Header));

			// Tag is required by API; assert if it mismatches the allocation record.
			if (h->tag != tag)
			{
				LOG_ERROR(Core, "SystemAllocator::free tag mismatch (expected={}, got={})", ToString(h->tag), ToString(tag));
			}

			detail::TrackFree(h->tag, h->size, 1);
			::operator delete(h->base);
		}
	};

	/// Dump a table of memory stats per tag using the logging system.
	inline void DumpStats()
	{
		LOG_INFO(Core, "Memory::DumpStats");
		for (size_t i = 0; i < static_cast<size_t>(MemTag::_Count); ++i)
		{
			const auto tag = static_cast<MemTag>(i);
			const auto& s = detail::g_stats[i];

			const uint64_t cur = s.currentBytes.load(std::memory_order_relaxed);
			const uint64_t peak = s.peakBytes.load(std::memory_order_relaxed);
			const uint64_t a = s.allocCount.load(std::memory_order_relaxed);
			const uint64_t f = s.freeCount.load(std::memory_order_relaxed);

			LOG_INFO(Core, "  {:<6} cur={}B peak={}B allocs={} frees={}", ToString(tag), cur, peak, a, f);
		}
	}
}

namespace engine::core::Memory
{
	/// Allocate aligned memory via `SystemAllocator` and track it under `tag`.
	inline void* alloc(size_t size, size_t align, engine::core::memory::MemTag tag)
	{
		return engine::core::memory::SystemAllocator::alloc(size, align, tag);
	}

	/// Free memory allocated by `Memory::alloc` and track the free under `tag`.
	inline void free(void* ptr, engine::core::memory::MemTag tag)
	{
		engine::core::memory::SystemAllocator::free(ptr, tag);
	}

	/// Dump per-tag allocation statistics (current/peak/count) to the log.
	inline void DumpStats()
	{
		engine::core::memory::DumpStats();
	}
}

