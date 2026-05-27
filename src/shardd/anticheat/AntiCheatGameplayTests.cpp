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
		// 5m en 1s = 5 m/s, sous 13.0 m/s default → OK.
		if (ac.CheckMovement(1, 5, 0, 0, 2000) != CheatVerdict::OK) return false;
		LOG_INFO(Core, "[AntiCheatTests] normal speed OK");
		return true;
	}

	bool TestSpeedHack()
	{
		AntiCheatGameplay ac;
		ac.CheckMovement(1, 0, 0, 0, 1000);
		// 30m en 1s = 30 m/s, > 19.5 m/s tolerance → SpeedHack.
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

	/// Regression : un joueur en sprint (touche Alt, 13 m/s côté CharacterController)
	/// envoyant ses inputs à 20 Hz (= 50 ms entre ticks, donc 0.65 m par tick) doit être
	/// accepté. Le bug d'origine : maxSpeedMps=7.5 → seuil 11.25 m/s, donc 13 m/s rejeté
	/// comme SpeedHack pendant les phases de sprint normales. Les logs de prod montraient
	/// 18+ rejets par minute dont la majorité sur ces déplacements légitimes.
	bool TestSprintAtTickRateNotRejected()
	{
		AntiCheatGameplay ac;
		// Sprint = 13 m/s, tick = 50 ms (20 Hz) → 0.65 m par tick.
		ac.CheckMovement(1, 0.0f, 0.0f, 0.0f, 0);
		for (int i = 1; i <= 20; ++i)  // 20 ticks = 1 seconde de sprint
		{
			const float x = static_cast<float>(i) * 0.65f;
			const uint64_t tsMs = static_cast<uint64_t>(i) * 50u;
			if (ac.CheckMovement(1, x, 0.0f, 0.0f, tsMs) != CheatVerdict::OK)
				return false;
		}
		LOG_INFO(Core, "[AntiCheatTests] sprint at tick rate accepted");
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
		&& TestSpeedHack() && TestTeleportHack()
		&& TestSprintAtTickRateNotRejected();
	if (ok) LOG_INFO(Core, "[AntiCheatTests] ALL OK");
	else LOG_ERROR(Core, "[AntiCheatTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
