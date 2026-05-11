#pragma once

#include <chrono>
#include <cstdint>

namespace engine::server
{
	/// Fixed-rate scheduler used by the headless server tick loop.
	class TickScheduler final
	{
	public:
		/// Construct an uninitialized scheduler.
		TickScheduler() = default;

		/// Reset scheduler state and release timing data.
		~TickScheduler();

		/// Configure the scheduler for a fixed tick rate.
		bool Init(uint32_t tickHz);

		/// Reset the scheduler and emit shutdown logs.
		void Shutdown();

		/// Start measuring time from now for the first tick.
		void Start();

		/// Sleep until the next scheduled tick and advance the tick counter.
		bool WaitForNextTick();

		/// Return the configured tick rate.
		uint32_t TickHz() const { return m_tickHz; }

		/// Return the number of ticks emitted since `Start()`.
		uint64_t TickCount() const { return m_tickCount; }

	private:
		uint32_t m_tickHz = 0;
		uint64_t m_tickCount = 0;
		bool m_started = false;
		std::chrono::steady_clock::time_point m_nextTickAt{};
		std::chrono::steady_clock::duration m_tickInterval{};
	};
}
