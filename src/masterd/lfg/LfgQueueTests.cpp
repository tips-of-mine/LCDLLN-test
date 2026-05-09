#include "engine/server/lfg/LfgQueue.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::lfg::LfgQueue;
	using engine::server::lfg::LfgRole;

	bool TestNotEnough()
	{
		LfgQueue q;
		q.Join(1, 100, LfgRole::Tank, 0);
		q.Join(1, 200, LfgRole::Healer, 0);
		// 2 DPS (need 3).
		q.Join(1, 300, LfgRole::Damage, 0);
		q.Join(1, 301, LfgRole::Damage, 0);
		auto m = q.TryMatch(1);
		if (m.has_value()) return false;
		LOG_INFO(Core, "[LfgQueueTests] not enough OK");
		return true;
	}

	bool TestFullGroup()
	{
		LfgQueue q;
		q.Join(1, 100, LfgRole::Tank, 0);
		q.Join(1, 200, LfgRole::Healer, 0);
		q.Join(1, 300, LfgRole::Damage, 0);
		q.Join(1, 301, LfgRole::Damage, 0);
		q.Join(1, 302, LfgRole::Damage, 0);
		auto m = q.TryMatch(1);
		if (!m.has_value()) return false;
		if (m->members.size() != 5) return false;
		if (q.QueueSize(1) != 0) return false;
		LOG_INFO(Core, "[LfgQueueTests] full group formed OK");
		return true;
	}

	bool TestLeave()
	{
		LfgQueue q;
		q.Join(1, 100, LfgRole::Tank, 0);
		if (q.QueueSize(1) != 1) return false;
		if (!q.Leave(1, 100)) return false;
		if (q.QueueSize(1) != 0) return false;
		if (q.Leave(1, 999)) return false;  // pas dans la queue
		LOG_INFO(Core, "[LfgQueueTests] leave OK");
		return true;
	}

	bool TestPerDungeon()
	{
		LfgQueue q;
		q.Join(1, 100, LfgRole::Tank, 0);
		q.Join(2, 200, LfgRole::Tank, 0);
		if (q.QueueSize(1) != 1 || q.QueueSize(2) != 1) return false;
		LOG_INFO(Core, "[LfgQueueTests] per-dungeon isolation OK");
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

	const bool ok = TestNotEnough() && TestFullGroup() && TestLeave() && TestPerDungeon();
	if (ok) LOG_INFO(Core, "[LfgQueueTests] ALL OK");
	else LOG_ERROR(Core, "[LfgQueueTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
