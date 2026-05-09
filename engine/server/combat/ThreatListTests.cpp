// CMANGOS.11 (Phase 3.11a) — Tests ThreatList.

#include "engine/server/combat/ThreatList.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::combat::ThreatList;

	bool TestEmpty()
	{
		ThreatList tl;
		if (tl.IsEngaged()) return false;
		if (tl.TopTarget().has_value()) return false;
		LOG_INFO(Core, "[ThreatListTests] empty OK");
		return true;
	}

	bool TestAddAndTop()
	{
		ThreatList tl;
		tl.AddThreat(100, 50.0f);
		tl.AddThreat(200, 30.0f);
		tl.AddThreat(300, 80.0f);
		auto top = tl.TopTarget();
		if (!top || *top != 300) return false;
		auto top3 = tl.TopN(3);
		if (top3.size() != 3 || top3[0] != 300 || top3[1] != 100 || top3[2] != 200) return false;
		LOG_INFO(Core, "[ThreatListTests] add + top OK");
		return true;
	}

	bool TestNegativeClamp()
	{
		ThreatList tl;
		tl.AddThreat(100, 50.0f);
		tl.AddThreat(100, -100.0f);  // doit clamp a 0
		if (tl.GetThreat(100) != 0.0f) return false;
		LOG_INFO(Core, "[ThreatListTests] negative clamp OK");
		return true;
	}

	bool TestDropAttacker()
	{
		ThreatList tl;
		tl.AddThreat(100, 50.0f);
		tl.AddThreat(200, 80.0f);
		tl.DropAttacker(200);
		if (tl.Size() != 1) return false;
		auto top = tl.TopTarget();
		if (!top || *top != 100) return false;
		LOG_INFO(Core, "[ThreatListTests] drop attacker OK");
		return true;
	}

	bool TestReset()
	{
		ThreatList tl;
		tl.AddThreat(1, 100.0f);
		tl.Reset();
		if (tl.IsEngaged()) return false;
		if (tl.TopTarget().has_value()) return false;
		LOG_INFO(Core, "[ThreatListTests] reset OK");
		return true;
	}

	bool TestRecomputeAfterChange()
	{
		ThreatList tl;
		tl.AddThreat(1, 50.0f);
		tl.AddThreat(2, 100.0f);
		auto t1 = tl.TopTarget();  // 2
		if (!t1 || *t1 != 2) return false;

		tl.AddThreat(1, 200.0f);  // 1 prend la tete (250)
		auto t2 = tl.TopTarget();
		if (!t2 || *t2 != 1) return false;

		LOG_INFO(Core, "[ThreatListTests] recompute after change OK");
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

	const bool ok = TestEmpty() && TestAddAndTop() && TestNegativeClamp()
		&& TestDropAttacker() && TestReset() && TestRecomputeAfterChange();

	if (ok) LOG_INFO(Core, "[ThreatListTests] ALL OK");
	else LOG_ERROR(Core, "[ThreatListTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
