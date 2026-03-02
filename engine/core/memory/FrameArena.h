#pragma once

#include "engine/core/memory/LinearArena.h"

#include <cstdint>
#include <vector>

namespace engine::core::memory
{
	/// FrameArena: N arenas (frames in flight) reset at the beginning of their frame.
	class FrameArena final
	{
	public:
		/// Create `framesInFlight` arenas, each with `perFrameCapacityBytes` capacity.
		FrameArena(uint32_t framesInFlight, size_t perFrameCapacityBytes)
			: m_framesInFlight(framesInFlight)
		{
			m_arenas.reserve(m_framesInFlight);
			for (uint32_t i = 0; i < m_framesInFlight; ++i)
			{
				m_arenas.emplace_back(perFrameCapacityBytes);
			}
		}

		/// Reset the arena corresponding to `frameIndex`.
		void BeginFrame(uint64_t frameIndex)
		{
			if (m_framesInFlight == 0)
			{
				return;
			}
			m_current = static_cast<uint32_t>(frameIndex % m_framesInFlight);
			m_arenas[m_current].reset();
		}

		/// Allocate from the current frame's arena (tracked by tag).
		void* alloc(size_t size, size_t align, MemTag tag)
		{
			return m_arenas[m_current].alloc(size, align, tag);
		}

		/// Return frames-in-flight count.
		uint32_t framesInFlight() const { return m_framesInFlight; }

	private:
		uint32_t m_framesInFlight = 0;
		uint32_t m_current = 0;
		std::vector<LinearArena> m_arenas;
	};
}

