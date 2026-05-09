#pragma once

#include "engine/core/Log.h"
#include "engine/core/memory/Memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

namespace engine::core::memory
{
	/// LinearArena: bump allocator with O(1) reset, no individual frees.
	class LinearArena final
	{
	public:
		/// Create an arena with a fixed-capacity backing store (bytes).
		explicit LinearArena(size_t capacityBytes)
			: m_capacity(capacityBytes)
		{
			m_base = static_cast<std::byte*>(::operator new(m_capacity, std::align_val_t(alignof(std::max_align_t))));
		}

		LinearArena(const LinearArena&) = delete;
		LinearArena& operator=(const LinearArena&) = delete;

		LinearArena(LinearArena&& other) noexcept
			: m_base(other.m_base)
			, m_capacity(other.m_capacity)
			, m_offset(other.m_offset)
			, m_bytesByTag(other.m_bytesByTag)
			, m_allocsByTag(other.m_allocsByTag)
		{
			other.m_base = nullptr;
			other.m_capacity = 0;
			other.m_offset = 0;
			other.m_bytesByTag.fill(0);
			other.m_allocsByTag.fill(0);
		}

		LinearArena& operator=(LinearArena&& other) noexcept
		{
			if (this == &other)
			{
				return *this;
			}
			Destroy();
			m_base = other.m_base;
			m_capacity = other.m_capacity;
			m_offset = other.m_offset;
			m_bytesByTag = other.m_bytesByTag;
			m_allocsByTag = other.m_allocsByTag;

			other.m_base = nullptr;
			other.m_capacity = 0;
			other.m_offset = 0;
			other.m_bytesByTag.fill(0);
			other.m_allocsByTag.fill(0);
			return *this;
		}

		/// Destroy the arena and release its backing memory.
		~LinearArena()
		{
			Destroy();
		}

		/// Allocate from the arena. Updates per-tag tracking; overflow aborts in debug builds.
		void* alloc(size_t size, size_t align, MemTag tag)
		{
			const size_t a = (align == 0) ? alignof(std::max_align_t) : align;
			const uintptr_t base = reinterpret_cast<uintptr_t>(m_base);
			const uintptr_t cur = base + m_offset;
			const uintptr_t aligned = (cur + (a - 1)) & ~(static_cast<uintptr_t>(a) - 1);
			const size_t newOffset = static_cast<size_t>(aligned - base + size);

			if (newOffset > m_capacity)
			{
				LOG_FATAL(Core, "LinearArena overflow (cap={}B, need={}B)", m_capacity, newOffset);
			}

			m_offset = newOffset;

			const size_t idx = static_cast<size_t>(tag);
			m_bytesByTag[idx] += static_cast<uint64_t>(size);
			m_allocsByTag[idx] += 1;
			detail::TrackAlloc(tag, static_cast<uint64_t>(size));

			return reinterpret_cast<void*>(aligned);
		}

		/// Reset the arena to empty (O(1)) and release all tracked bytes for this arena.
		void reset()
		{
			for (size_t i = 0; i < static_cast<size_t>(MemTag::_Count); ++i)
			{
				const auto tag = static_cast<MemTag>(i);
				const uint64_t bytes = m_bytesByTag[i];
				const uint64_t allocs = m_allocsByTag[i];
				if (bytes > 0 || allocs > 0)
				{
					detail::TrackFree(tag, bytes, allocs);
				}
				m_bytesByTag[i] = 0;
				m_allocsByTag[i] = 0;
			}
			m_offset = 0;
		}

		/// Return arena capacity (bytes).
		size_t capacity() const { return m_capacity; }

	private:
		void Destroy()
		{
			if (m_base)
			{
				// Ensure tracking doesn't leak "current" bytes when the arena is destroyed.
				reset();
				::operator delete(m_base, std::align_val_t(alignof(std::max_align_t)));
				m_base = nullptr;
			}
			m_capacity = 0;
			m_offset = 0;
		}

		std::byte* m_base = nullptr;
		size_t m_capacity = 0;
		size_t m_offset = 0;

		std::array<uint64_t, static_cast<size_t>(MemTag::_Count)> m_bytesByTag{};
		std::array<uint64_t, static_cast<size_t>(MemTag::_Count)> m_allocsByTag{};
	};
}

