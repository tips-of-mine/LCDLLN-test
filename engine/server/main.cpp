#include "engine/server/ServerApp.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string_view>
#include <utility>

namespace
{
	/// Return a log level matching the text value stored in config.
	engine::core::LogLevel ParseLogLevel(std::string_view text)
	{
		if (text == "Trace" || text == "trace") return engine::core::LogLevel::Trace;
		if (text == "Debug" || text == "debug") return engine::core::LogLevel::Debug;
		if (text == "Info" || text == "info") return engine::core::LogLevel::Info;
		if (text == "Warn" || text == "warn") return engine::core::LogLevel::Warn;
		if (text == "Error" || text == "error") return engine::core::LogLevel::Error;
		if (text == "Fatal" || text == "fatal") return engine::core::LogLevel::Fatal;
		if (text == "Off" || text == "off") return engine::core::LogLevel::Off;
		return engine::core::LogLevel::Info;
	}

	/// Return true when the argument list contains an exact flag.
	bool HasCliFlag(int argc, char** argv, std::string_view flag)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (argv[i] != nullptr && std::string_view(argv[i]) == flag)
			{
				return true;
			}
		}
		return false;
	}

	engine::server::ServerApp* g_serverApp = nullptr;

	/// Request a graceful stop when the Windows console is closing.
	BOOL WINAPI HandleConsoleCtrl(DWORD controlType)
	{
		switch (controlType)
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			if (g_serverApp != nullptr)
			{
				g_serverApp->RequestStop();
				return TRUE;
			}
			return FALSE;
		default:
			return FALSE;
		}
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);

	engine::core::LogSettings logSettings;
	logSettings.level = ParseLogLevel(config.GetString("log.level", "Info"));
	logSettings.console = config.GetBool("log.console", true) || HasCliFlag(argc, argv, "-console");
	logSettings.flushAlways = true;
	logSettings.filePath = HasCliFlag(argc, argv, "-log")
		? engine::core::Log::MakeTimestampedFilename("lcdlln_server.exe")
		: config.GetString("log.file", "engine.log");
	logSettings.rotation_size_mb = static_cast<size_t>(std::max(static_cast<int64_t>(0), config.GetInt("log.rotation_size_mb", 10)));
	logSettings.retention_days = static_cast<int>(config.GetInt("log.retention_days", 7));
	engine::core::Log::Init(logSettings);
	LOG_INFO(Core, "[ServerMain] Log initialized (console={}, file={})",
		logSettings.console ? "on" : "off",
		logSettings.filePath.empty() ? "<disabled>" : logSettings.filePath);

	engine::server::ServerApp app(std::move(config));
	g_serverApp = &app;
	if (!SetConsoleCtrlHandler(HandleConsoleCtrl, TRUE))
	{
		LOG_WARN(Core, "[ServerMain] SetConsoleCtrlHandler failed");
	}

	int result = 1;
	if (!app.Init())
	{
		LOG_ERROR(Core, "[ServerMain] Init FAILED");
	}
	else
	{
		LOG_INFO(Core, "[ServerMain] Init OK");
		result = app.Run();
	}

	app.Shutdown();
	g_serverApp = nullptr;
	LOG_INFO(Core, "[ServerMain] Shutdown complete (result={})", result);
	engine::core::Log::Shutdown();
	return result;
}
