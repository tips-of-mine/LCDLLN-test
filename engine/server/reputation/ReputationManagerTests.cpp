// CMANGOS.24 (Phase 3.24a) — Tests ReputationManager + Spillover.

#include "engine/server/reputation/ReputationManager.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::reputation::ReputationManager;
	using engine::server::reputation::ReputationStanding;
	using engine::server::reputation::SpilloverRule;
	using engine::server::reputation::kReputationMax;
	using engine::server::reputation::kReputationMin;

	bool TestBasicGainAndQuery()
	{
		ReputationManager mgr;
		mgr.GainReputation(1, 100, 500);
		if (mgr.GetReputation(1, 100) != 500) return false;
		mgr.GainReputation(1, 100, 300);
		if (mgr.GetReputation(1, 100) != 800) return false;
		LOG_INFO(Core, "[ReputationManagerTests] basic gain OK");
		return true;
	}

	bool TestClamping()
	{
		ReputationManager mgr;
		mgr.GainReputation(1, 100, 100000);
		if (mgr.GetReputation(1, 100) != kReputationMax) return false;
		mgr.GainReputation(1, 200, -100000);
		if (mgr.GetReputation(1, 200) != kReputationMin) return false;
		LOG_INFO(Core, "[ReputationManagerTests] clamp OK");
		return true;
	}

	bool TestStandingThresholds()
	{
		if (ReputationManager::StandingFor(-42000) != ReputationStanding::Hated) return false;
		if (ReputationManager::StandingFor(-1) != ReputationStanding::Neutral) return false;
		if (ReputationManager::StandingFor(0) != ReputationStanding::Neutral) return false;
		if (ReputationManager::StandingFor(1500) != ReputationStanding::Friendly) return false;
		if (ReputationManager::StandingFor(8000) != ReputationStanding::Honored) return false;
		if (ReputationManager::StandingFor(20000) != ReputationStanding::Revered) return false;
		if (ReputationManager::StandingFor(40000) != ReputationStanding::Exalted) return false;
		LOG_INFO(Core, "[ReputationManagerTests] standing thresholds OK");
		return true;
	}

	bool TestSpillover()
	{
		ReputationManager mgr;
		// Faction 100 gain → 50% sur 200, 25% sur 300.
		mgr.AddSpilloverRule({100, 200, 0.5f});
		mgr.AddSpilloverRule({100, 300, 0.25f});

		mgr.GainReputation(1, 100, 1000);
		if (mgr.GetReputation(1, 100) != 1000) return false;
		if (mgr.GetReputation(1, 200) != 500) return false;
		if (mgr.GetReputation(1, 300) != 250) return false;
		LOG_INFO(Core, "[ReputationManagerTests] spillover OK");
		return true;
	}

	bool TestSpilloverNotReverse()
	{
		ReputationManager mgr;
		mgr.AddSpilloverRule({100, 200, 0.5f});
		// Gain sur 200 ne doit PAS spillover sur 100 (rule sens unique).
		mgr.GainReputation(1, 200, 1000);
		if (mgr.GetReputation(1, 100) != 0) return false;
		LOG_INFO(Core, "[ReputationManagerTests] spillover unidirectional OK");
		return true;
	}

	bool TestMultiAccountIsolation()
	{
		ReputationManager mgr;
		mgr.GainReputation(1, 100, 500);
		mgr.GainReputation(2, 100, 1000);
		if (mgr.GetReputation(1, 100) != 500) return false;
		if (mgr.GetReputation(2, 100) != 1000) return false;
		if (mgr.GetReputation(3, 100) != 0) return false;
		LOG_INFO(Core, "[ReputationManagerTests] multi-account isolation OK");
		return true;
	}

	bool TestSpilloverNegative()
	{
		ReputationManager mgr;
		mgr.AddSpilloverRule({100, 200, 0.5f});
		mgr.GainReputation(1, 100, -2000);  // perte
		if (mgr.GetReputation(1, 100) != -2000) return false;
		if (mgr.GetReputation(1, 200) != -1000) return false;  // 50% de la perte
		LOG_INFO(Core, "[ReputationManagerTests] spillover negative OK");
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

	const bool ok = TestBasicGainAndQuery() && TestClamping()
		&& TestStandingThresholds() && TestSpillover()
		&& TestSpilloverNotReverse() && TestMultiAccountIsolation()
		&& TestSpilloverNegative();

	if (ok) LOG_INFO(Core, "[ReputationManagerTests] ALL OK");
	else LOG_ERROR(Core, "[ReputationManagerTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
