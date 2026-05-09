#include "engine/server/maps/InstanceManager.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::maps::InstanceId;
	using engine::server::maps::InstanceManager;
	using engine::server::maps::InstanceState;

	bool TestCreateAndFind()
	{
		InstanceManager mgr;
		const InstanceId a = mgr.Create(1, 100);
		const InstanceId b = mgr.Create(1, 200);
		if (a == b) return false;  // unique IDs
		auto info = mgr.Find(a);
		if (!info || info->mapId != 1 || info->state != InstanceState::Active) return false;
		if (mgr.Size() != 2) return false;
		LOG_INFO(Core, "[InstanceManagerTests] Create + Find OK");
		return true;
	}

	bool TestLifecycle()
	{
		InstanceManager mgr;
		const InstanceId id = mgr.Create(1, 100);
		mgr.TouchActivity(id, 200);
		auto info = mgr.Find(id);
		if (!info || info->lastActiveTsMs != 200) return false;
		mgr.MarkIdle(id);
		info = mgr.Find(id);
		if (!info || info->state != InstanceState::Idle) return false;
		mgr.Despawn(id);
		info = mgr.Find(id);
		if (!info || info->state != InstanceState::Despawned) return false;
		LOG_INFO(Core, "[InstanceManagerTests] lifecycle OK");
		return true;
	}

	bool TestGarbageCollect()
	{
		InstanceManager mgr;
		const InstanceId a = mgr.Create(1, 100);
		const InstanceId b = mgr.Create(1, 200);
		const InstanceId c = mgr.Create(2, 300);
		mgr.MarkIdle(a);  // Idle a t=100
		mgr.Despawn(b);   // Despawned

		// GC at t=10000 with idleUnload=5000 → a (Idle since 100, > 5100) is freed; b freed; c kept.
		const auto freed = mgr.GarbageCollect(10000, 5000);
		if (freed != 2) return false;
		if (mgr.Size() != 1) return false;
		if (!mgr.Find(c)) return false;
		LOG_INFO(Core, "[InstanceManagerTests] GC OK");
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

	const bool ok = TestCreateAndFind() && TestLifecycle() && TestGarbageCollect();

	if (ok) LOG_INFO(Core, "[InstanceManagerTests] ALL OK");
	else LOG_ERROR(Core, "[InstanceManagerTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
