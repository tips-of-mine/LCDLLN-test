#include "src/shared/core/Clock.h"

namespace engine::core
{
	IClock::TimePoint SteadyClock::Now() const
	{
		return std::chrono::steady_clock::now();
	}

	SteadyClock& SteadyClock::Instance()
	{
		static SteadyClock s_instance;
		return s_instance;
	}

	FakeClock::FakeClock()
		: m_now(TimePoint{})
	{
	}

	IClock::TimePoint FakeClock::Now() const
	{
		return m_now;
	}

	void FakeClock::AdvanceMs(int64_t deltaMs)
	{
		m_now += std::chrono::milliseconds(deltaMs);
	}

	void FakeClock::AdvanceSec(int64_t deltaSec)
	{
		m_now += std::chrono::seconds(deltaSec);
	}

	void FakeClock::SetNow(TimePoint tp)
	{
		m_now = tp;
	}
}
