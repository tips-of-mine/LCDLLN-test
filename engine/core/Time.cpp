#include "engine/core/Time.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace engine::core
{
	uint64_t Time::NowTicks()
	{
		using namespace std::chrono;
		return static_cast<uint64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
	}

	Time::Time(uint32_t fpsWindowSize)
		: m_windowSize(std::max<uint32_t>(1, fpsWindowSize))
		, m_windowDeltas(m_windowSize, 0.0)
	{
		m_lastTicks = NowTicks();
	}

	void Time::BeginFrame()
	{
		const uint64_t nowTicks = NowTicks();
		uint64_t diff = (nowTicks >= m_lastTicks) ? (nowTicks - m_lastTicks) : 0;
		m_lastTicks = nowTicks;

		const double seconds = static_cast<double>(diff) * 1e-9;
		m_deltaSeconds = std::clamp(seconds, 0.0, m_maxDeltaSeconds);

		// Sliding window FPS smoothing: maintain sum of deltas for up to m_windowSize frames.
		if (m_windowCount < m_windowSize)
		{
			m_windowDeltas[m_windowCursor] = m_deltaSeconds;
			m_sumWindowSeconds += m_deltaSeconds;
			++m_windowCount;
		}
		else
		{
			m_sumWindowSeconds -= m_windowDeltas[m_windowCursor];
			m_windowDeltas[m_windowCursor] = m_deltaSeconds;
			m_sumWindowSeconds += m_deltaSeconds;
		}

		m_windowCursor = (m_windowCursor + 1) % m_windowSize;

		if (m_sumWindowSeconds > 0.0)
		{
			m_fps = static_cast<double>(m_windowCount) / m_sumWindowSeconds;
		}
		else
		{
			m_fps = 0.0;
		}

		++m_frameIndex;
	}

	void Time::EndFrame()
	{
		// Intentionally empty: `BeginFrame()` computes delta based on monotonic time.
	}

	double Time::DeltaSeconds() const
	{
		return m_deltaSeconds;
	}

	double Time::FPS() const
	{
		return m_fps;
	}

	uint64_t Time::FrameIndex() const
	{
		return m_frameIndex;
	}

	void Time::SetMaxDeltaSeconds(double maxDeltaSeconds)
	{
		m_maxDeltaSeconds = std::max(0.0, maxDeltaSeconds);
	}
}

