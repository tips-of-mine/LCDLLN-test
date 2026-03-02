#pragma once

#include <cstdint>
#include <vector>

namespace engine::core
{
	class Time final
	{
	public:
		/// Return current monotonic ticks in nanoseconds (arbitrary epoch).
		static uint64_t NowTicks();

		/// Construct a frame timer with an FPS smoothing window.
		explicit Time(uint32_t fpsWindowSize = 120);

		/// Mark the start of a new frame (updates delta + frameIndex + FPS smoothing).
		void BeginFrame();

		/// Mark the end of a frame (currently a no-op, kept for symmetry/extension).
		void EndFrame();

		/// Delta time of the most recent frame in seconds (clamped, never negative).
		double DeltaSeconds() const;

		/// Smoothed FPS computed over the configured window.
		double FPS() const;

		/// Current frame index (increments on each `BeginFrame()`).
		uint64_t FrameIndex() const;

		/// Set maximum allowed delta (seconds) used for clamping.
		void SetMaxDeltaSeconds(double maxDeltaSeconds);

	private:
		uint32_t m_windowSize = 120;
		double m_maxDeltaSeconds = 0.1;

		uint64_t m_frameIndex = 0;
		double m_deltaSeconds = 0.0;
		double m_fps = 0.0;

		uint64_t m_lastTicks = 0;

		double m_sumWindowSeconds = 0.0;
		uint32_t m_windowCount = 0;
		uint32_t m_windowCursor = 0;
		std::vector<double> m_windowDeltas;
	};
}

