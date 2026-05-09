#include "src/masterd/world/WorldStateRegistry.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::world::WorldStateRegistry;

	bool TestSetGet()
	{
		WorldStateRegistry r;
		if (r.Get(1) != 0) return false;
		r.Set(1, 42);
		if (r.Get(1) != 42) return false;
		LOG_INFO(Core, "[WorldStateTests] set/get OK");
		return true;
	}

	bool TestIncrement()
	{
		WorldStateRegistry r;
		if (r.Increment(1) != 1) return false;
		if (r.Increment(1, 5) != 6) return false;
		if (r.Increment(1, -10) != -4) return false;
		LOG_INFO(Core, "[WorldStateTests] increment OK");
		return true;
	}

	bool TestSubscribe()
	{
		WorldStateRegistry r;
		int callCount = 0;
		int64_t lastOld = -1, lastNew = -1;
		r.Subscribe([&](uint32_t, int64_t o, int64_t n) {
			++callCount; lastOld = o; lastNew = n;
		});
		r.Set(1, 100);
		if (callCount != 1) return false;
		if (lastOld != 0 || lastNew != 100) return false;

		// Set same value → no notify.
		r.Set(1, 100);
		if (callCount != 1) return false;
		LOG_INFO(Core, "[WorldStateTests] subscribe OK");
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

	const bool ok = TestSetGet() && TestIncrement() && TestSubscribe();
	if (ok) LOG_INFO(Core, "[WorldStateTests] ALL OK");
	else LOG_ERROR(Core, "[WorldStateTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
