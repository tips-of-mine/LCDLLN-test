#include "src/shared/platform/CrashContext.h"
#include "src/shared/core/Log.h"
#include <string>

namespace
{
	using namespace engine::server::platform;

	bool TestFormat()
	{
		CrashContext c;
		c.buildHash      = "deadbeef";
		c.uptimeMs       = 12345;
		c.lastTickMs     = 12340;
		c.lastZone       = "Stranglethorn";
		c.activeSessions = 17;
		c.signalName     = "SIGSEGV";
		auto s = Format(c);
		if (s.find("buildHash: deadbeef") == std::string::npos) return false;
		if (s.find("uptimeMs: 12345")     == std::string::npos) return false;
		if (s.find("lastZone: Stranglethorn") == std::string::npos) return false;
		if (s.find("signal: SIGSEGV")     == std::string::npos) return false;
		LOG_INFO(Core, "[CrashContextTests] format OK");
		return true;
	}

	bool TestIsValid()
	{
		CrashContext c;
		if (IsValid(c)) return false; // vide
		c.buildHash = "abc";
		if (IsValid(c)) return false; // signal manquant
		c.signalName = "SIGABRT";
		if (!IsValid(c)) return false;
		LOG_INFO(Core, "[CrashContextTests] isvalid OK");
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

	const bool ok = TestFormat() && TestIsValid();
	if (ok) LOG_INFO(Core, "[CrashContextTests] ALL OK");
	else LOG_ERROR(Core, "[CrashContextTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
