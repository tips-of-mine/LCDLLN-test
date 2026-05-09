#include "engine/server/TickScheduler.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <thread>

namespace engine::server
{
	TickScheduler::~TickScheduler()
	{
		Shutdown();
	}

	bool TickScheduler::Init(uint32_t tickHz)
	{
		if (tickHz == 0)
		{
			LOG_ERROR(Core, "[TickScheduler] Init FAILED: tick_hz must be > 0");
			return false;
		}

		m_tickHz = tickHz;
		m_tickCount = 0;
		m_started = false;
		m_tickInterval = std::chrono::microseconds(1000000 / static_cast<int64_t>(m_tickHz));
		LOG_INFO(Core, "[TickScheduler] Init OK (tick_hz={}, interval_us={})",
			m_tickHz,
			std::chrono::duration_cast<std::chrono::microseconds>(m_tickInterval).count());
		return true;
	}

	void TickScheduler::Shutdown()
	{
		if (m_tickHz != 0 || m_tickCount != 0 || m_started)
		{
			LOG_INFO(Core, "[TickScheduler] Destroyed (tick_hz={}, ticks={})", m_tickHz, m_tickCount);
		}

		m_tickHz = 0;
		m_tickCount = 0;
		m_started = false;
		m_tickInterval = std::chrono::steady_clock::duration::zero();
		m_nextTickAt = {};
	}

	void TickScheduler::Start()
	{
		m_tickCount = 0;
		m_started = true;
		m_nextTickAt = std::chrono::steady_clock::now();
		LOG_INFO(Core, "[TickScheduler] Start OK");
	}

	bool TickScheduler::WaitForNextTick()
	{
		if (m_tickHz == 0 || m_tickInterval == std::chrono::steady_clock::duration::zero())
		{
			LOG_ERROR(Core, "[TickScheduler] WaitForNextTick FAILED: scheduler not initialized");
			return false;
		}

		if (!m_started)
		{
			Start();
		}

		const auto now = std::chrono::steady_clock::now();
		if (now < m_nextTickAt)
		{
			std::this_thread::sleep_until(m_nextTickAt);
		}
		else if (now - m_nextTickAt > (m_tickInterval * 2))
		{
			LOG_WARN(Core, "[TickScheduler] Tick drift detected; resyncing schedule");
			m_nextTickAt = now;
		}

		m_nextTickAt += m_tickInterval;
		++m_tickCount;
		return true;
	}
}
