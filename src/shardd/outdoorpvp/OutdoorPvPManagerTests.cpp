#include "engine/server/outdoorpvp/OutdoorPvPManager.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::outdoorpvp::OutdoorPvPManager;
	using engine::server::outdoorpvp::Zone;
	using engine::server::outdoorpvp::Objective;

	bool TestUnknown()
	{
		OutdoorPvPManager m;
		if (m.BeginCapture(1, 1, 0)) return false;
		if (m.TickCapture(1, 1, 50)) return false;
		if (m.Score(1, 0) != 0) return false;
		LOG_INFO(Core, "[OutdoorPvPTests] unknown zone OK");
		return true;
	}

	bool TestCaptureFlow()
	{
		OutdoorPvPManager m;
		Zone z;
		z.id = 10;
		z.objectives.push_back(Objective{1, 0xFF, 0, 0xFF});
		m.RegisterZone(z);

		if (!m.BeginCapture(10, 1, 0)) return false;     // Alliance commence
		if (m.TickCapture(10, 1, 40)) return false;      // pas encore complet
		if (m.TickCapture(10, 1, 30)) return false;      // total 70
		if (!m.TickCapture(10, 1, 30)) return false;     // 100 -> capture terminee
		if (m.Score(10, 0) != 1) return false;
		LOG_INFO(Core, "[OutdoorPvPTests] capture flow OK");
		return true;
	}

	bool TestSwitchFactionResets()
	{
		OutdoorPvPManager m;
		Zone z;
		z.id = 20;
		z.objectives.push_back(Objective{2, 0xFF, 0, 0xFF});
		m.RegisterZone(z);

		m.BeginCapture(20, 2, 0);
		m.TickCapture(20, 2, 60); // Alliance a 60
		m.BeginCapture(20, 2, 1); // Horde reprend -> reset a 0
		// Si pas reset, 60 + 50 = 110 -> capture immediate. Avec reset, 50 < 100.
		if (m.TickCapture(20, 2, 50)) return false;
		if (m.Score(20, 1) != 0) return false;
		LOG_INFO(Core, "[OutdoorPvPTests] switch faction resets OK");
		return true;
	}

	bool TestReset()
	{
		OutdoorPvPManager m;
		Zone z;
		z.id = 30;
		z.objectives.push_back(Objective{3, 0xFF, 0, 0xFF});
		m.RegisterZone(z);
		m.BeginCapture(30, 3, 0);
		m.TickCapture(30, 3, 100); // Alliance gagne, score=1
		if (m.Score(30, 0) != 1) return false;
		m.ResetZone(30);
		if (m.Score(30, 0) != 0) return false;
		LOG_INFO(Core, "[OutdoorPvPTests] reset OK");
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

	const bool ok = TestUnknown() && TestCaptureFlow() && TestSwitchFactionResets() && TestReset();
	if (ok) LOG_INFO(Core, "[OutdoorPvPTests] ALL OK");
	else LOG_ERROR(Core, "[OutdoorPvPTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
