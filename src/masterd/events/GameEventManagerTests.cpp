#include "engine/server/events/GameEventManager.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::events::EventState;
	using engine::server::events::GameEventDef;
	using engine::server::events::GameEventManager;

	bool TestOneShotActiveWindow()
	{
		GameEventManager mgr;
		GameEventDef e;
		e.id = 1;
		e.name = "Christmas";
		e.startTsMs = 1000;
		e.durationMs = 500;
		mgr.Register(e);

		if (mgr.GetState(1, 500) != EventState::Inactive) return false;
		if (mgr.GetState(1, 1000) != EventState::Active) return false;
		if (mgr.GetState(1, 1499) != EventState::Active) return false;
		if (mgr.GetState(1, 1500) != EventState::Inactive) return false;  // exclusive
		if (mgr.GetState(1, 2000) != EventState::Inactive) return false;
		LOG_INFO(Core, "[GameEventManagerTests] one-shot OK");
		return true;
	}

	bool TestRecurring()
	{
		GameEventManager mgr;
		GameEventDef e;
		e.id = 2;
		e.startTsMs = 0;
		e.durationMs = 100;
		e.recurMs = 1000;  // first 100ms of every 1000
		mgr.Register(e);

		if (mgr.GetState(2, 50) != EventState::Active) return false;
		if (mgr.GetState(2, 150) != EventState::Inactive) return false;
		if (mgr.GetState(2, 1050) != EventState::Active) return false;  // 2nd cycle
		if (mgr.GetState(2, 2099) != EventState::Active) return false;
		if (mgr.GetState(2, 2100) != EventState::Inactive) return false;
		LOG_INFO(Core, "[GameEventManagerTests] recurring OK");
		return true;
	}

	bool TestActiveEvents()
	{
		GameEventManager mgr;
		GameEventDef a; a.id = 1; a.startTsMs = 0; a.durationMs = 1000; mgr.Register(a);
		GameEventDef b; b.id = 2; b.startTsMs = 500; b.durationMs = 1000; mgr.Register(b);
		GameEventDef c; c.id = 3; c.startTsMs = 5000; c.durationMs = 100; mgr.Register(c);

		auto active = mgr.ActiveEvents(700);
		if (active.size() != 2) return false;
		auto active2 = mgr.ActiveEvents(5050);
		if (active2.size() != 1 || active2[0] != 3) return false;
		LOG_INFO(Core, "[GameEventManagerTests] active list OK");
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

	const bool ok = TestOneShotActiveWindow() && TestRecurring() && TestActiveEvents();
	if (ok) LOG_INFO(Core, "[GameEventManagerTests] ALL OK");
	else LOG_ERROR(Core, "[GameEventManagerTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
