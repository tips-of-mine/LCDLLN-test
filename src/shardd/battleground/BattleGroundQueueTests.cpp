#include "src/shardd/battleground/BattleGroundQueue.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::battleground;

	bool TestEmpty()
	{
		BattleGroundQueue q;
		if (q.Size(1) != 0) return false;
		auto m = q.TryMakeMatch(1, 5);
		if (m.has_value()) return false;
		LOG_INFO(Core, "[BgQueueTests] empty OK");
		return true;
	}

	bool TestImbalanced()
	{
		BattleGroundQueue q;
		// 4 alliance, 0 horde => pas de match
		for (int i = 0; i < 4; ++i) q.Enqueue(1, 100 + i, 0, 1000 + i);
		auto m = q.TryMakeMatch(1, 2);
		if (m.has_value()) return false;
		if (q.Size(1) != 4) return false;
		LOG_INFO(Core, "[BgQueueTests] imbalanced rejected OK");
		return true;
	}

	bool TestMatch5v5()
	{
		BattleGroundQueue q;
		// 7 alliance + 6 horde => peut former 5v5, restent 2A + 1H
		for (int i = 0; i < 7; ++i) q.Enqueue(2, 100 + i, 0, 1000 + i);
		for (int i = 0; i < 6; ++i) q.Enqueue(2, 200 + i, 1, 2000 + i);
		auto m = q.TryMakeMatch(2, 5);
		if (!m) return false;
		if (m->alliance.size() != 5 || m->horde.size() != 5) return false;
		// FIFO : alliance picks should be 100..104
		if (m->alliance[0] != 100 || m->alliance[4] != 104) return false;
		if (m->horde[0]    != 200 || m->horde[4]    != 204) return false;
		// Restent 2 alliance + 1 horde
		if (q.Size(2) != 3) return false;
		LOG_INFO(Core, "[BgQueueTests] match 5v5 OK");
		return true;
	}

	bool TestExactSize()
	{
		BattleGroundQueue q;
		q.Enqueue(3, 1, 0, 100);
		q.Enqueue(3, 2, 1, 101);
		auto m = q.TryMakeMatch(3, 1);
		if (!m) return false;
		if (m->alliance.size() != 1 || m->horde.size() != 1) return false;
		if (q.Size(3) != 0) return false;
		LOG_INFO(Core, "[BgQueueTests] exact size OK");
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

	const bool ok = TestEmpty() && TestImbalanced() && TestMatch5v5() && TestExactSize();
	if (ok) LOG_INFO(Core, "[BgQueueTests] ALL OK");
	else LOG_ERROR(Core, "[BgQueueTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
