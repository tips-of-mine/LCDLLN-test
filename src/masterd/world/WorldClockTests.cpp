#include "src/masterd/world/WorldClock.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::world;

	bool TestRealtime()
	{
		WorldClock c(1000, 1.0);
		// 1h plus tard reel = 1h plus tard en jeu
		const uint64_t hour = 3600ull * 1000ull;
		if (c.GameTimeMs(1000 + hour) != hour) return false;
		if (c.HourOfDay(1000 + hour)  != 1) return false;
		LOG_INFO(Core, "[WorldClockTests] realtime OK");
		return true;
	}

	bool TestSpeedX60()
	{
		WorldClock c(0, 60.0);
		// 1 minute reelle = 1 heure en jeu
		const uint64_t minute = 60ull * 1000ull;
		if (c.HourOfDay(minute) != 1) return false;
		// 24 minutes = 1 jour complet -> hour 0
		if (c.HourOfDay(24 * minute) != 0) return false;
		LOG_INFO(Core, "[WorldClockTests] speed x60 OK");
		return true;
	}

	bool TestPhases()
	{
		WorldClock c(0, 60.0);
		// 1 min reelle = 1h jeu
		const uint64_t minute = 60ull * 1000ull;
		// 0h = Night
		if (c.Phase(0)         != DayPhase::Night) return false;
		// 6h = Dawn
		if (c.Phase(6 * minute) != DayPhase::Dawn) return false;
		// 12h = Day
		if (c.Phase(12 * minute) != DayPhase::Day) return false;
		// 19h = Dusk
		if (c.Phase(19 * minute) != DayPhase::Dusk) return false;
		// 23h = Night
		if (c.Phase(23 * minute) != DayPhase::Night) return false;
		LOG_INFO(Core, "[WorldClockTests] phases OK");
		return true;
	}

	bool TestBeforeEpoch()
	{
		WorldClock c(1000, 1.0);
		if (c.GameTimeMs(500) != 0) return false;
		LOG_INFO(Core, "[WorldClockTests] before epoch OK");
		return true;
	}

	bool TestSetSpeed()
	{
		WorldClock c(0, 1.0);
		c.SetSpeed(-5.0);
		if (c.Speed() != 0.0) return false; // floor a 0
		c.SetSpeed(2.5);
		if (c.Speed() != 2.5) return false;
		LOG_INFO(Core, "[WorldClockTests] set speed OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestRealtime() && TestSpeedX60() && TestPhases()
	             && TestBeforeEpoch() && TestSetSpeed();
	if (ok) LOG_INFO(Core, "[WorldClockTests] ALL OK");
	else LOG_ERROR(Core, "[WorldClockTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
