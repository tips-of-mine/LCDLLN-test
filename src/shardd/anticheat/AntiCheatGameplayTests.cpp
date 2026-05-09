#include "src/shardd/anticheat/AntiCheatGameplay.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::anticheat::AntiCheatGameplay;
	using engine::server::anticheat::CheatVerdict;

	bool TestFirstReportOK()
	{
		AntiCheatGameplay ac;
		if (ac.CheckMovement(1, 0, 0, 0, 1000) != CheatVerdict::OK) return false;
		LOG_INFO(Core, "[AntiCheatTests] first report OK");
		return true;
	}

	bool TestNormalSpeed()
	{
		AntiCheatGameplay ac;
		ac.CheckMovement(1, 0, 0, 0, 1000);
		// 5m en 1s = 5 m/s, sous 7.5 m/s default → OK.
		if (ac.CheckMovement(1, 5, 0, 0, 2000) != CheatVerdict::OK) return false;
		LOG_INFO(Core, "[AntiCheatTests] normal speed OK");
		return true;
	}

	bool TestSpeedHack()
	{
		AntiCheatGameplay ac;
		ac.CheckMovement(1, 0, 0, 0, 1000);
		// 30m en 1s = 30 m/s, > 11.25 m/s tolerance → SpeedHack.
		if (ac.CheckMovement(1, 30, 0, 0, 2000) != CheatVerdict::SpeedHack) return false;
		LOG_INFO(Core, "[AntiCheatTests] speed hack detected");
		return true;
	}

	bool TestTeleportHack()
	{
		AntiCheatGameplay ac;
		ac.CheckMovement(1, 0, 0, 0, 1000);
		// 100m d'un coup → TeleportHack peu importe le temps.
		if (ac.CheckMovement(1, 100, 0, 0, 100000) != CheatVerdict::TeleportHack) return false;
		LOG_INFO(Core, "[AntiCheatTests] teleport hack detected");
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

	const bool ok = TestFirstReportOK() && TestNormalSpeed()
		&& TestSpeedHack() && TestTeleportHack();
	if (ok) LOG_INFO(Core, "[AntiCheatTests] ALL OK");
	else LOG_ERROR(Core, "[AntiCheatTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
