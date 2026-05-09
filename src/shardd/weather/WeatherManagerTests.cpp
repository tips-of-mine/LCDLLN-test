#include "engine/server/weather/WeatherManager.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::weather::WeatherKind;
	using engine::server::weather::WeatherManager;
	using engine::server::weather::ZoneWeatherProfile;

	bool TestRegisterAndDefault()
	{
		WeatherManager mgr;
		ZoneWeatherProfile p;
		p.zoneId = 1;
		mgr.RegisterZone(p);
		if (mgr.ZoneCount() != 1) return false;
		auto s = mgr.GetState(1);
		if (s.kind != WeatherKind::Clear) return false;
		LOG_INFO(Core, "[WeatherManagerTests] register OK");
		return true;
	}

	bool TestDeterministicTick()
	{
		WeatherManager mgr;
		ZoneWeatherProfile p;
		p.zoneId = 1;
		p.changeIntervalMs = 1000;
		mgr.RegisterZone(p);

		std::mt19937 rng(42);
		mgr.Tick(0, rng);
		auto s1 = mgr.GetState(1);
		// Apres tick a t=0, nextChange = 1000.
		if (s1.nextChangeTsMs != 1000) return false;
		// Intensity dans [0.3, 1.0].
		if (s1.intensity < 0.29f || s1.intensity > 1.01f) return false;

		// Tick a 500 → no change (gate).
		mgr.Tick(500, rng);
		auto s2 = mgr.GetState(1);
		if (s2.nextChangeTsMs != 1000) return false;
		if (s2.kind != s1.kind) return false;

		// Tick a 1500 → reroll.
		mgr.Tick(1500, rng);
		auto s3 = mgr.GetState(1);
		if (s3.nextChangeTsMs != 2500) return false;
		LOG_INFO(Core, "[WeatherManagerTests] deterministic tick + gate OK");
		return true;
	}

	bool TestUnknownZone()
	{
		WeatherManager mgr;
		auto s = mgr.GetState(999);
		if (s.kind != WeatherKind::Clear) return false;
		LOG_INFO(Core, "[WeatherManagerTests] unknown zone default OK");
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

	const bool ok = TestRegisterAndDefault() && TestDeterministicTick() && TestUnknownZone();
	if (ok) LOG_INFO(Core, "[WeatherManagerTests] ALL OK");
	else LOG_ERROR(Core, "[WeatherManagerTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
