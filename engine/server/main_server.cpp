#include "engine/server/ServerApp.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
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

	/// Apply server-specific CLI: --port=3840 or --port 3840 (and -port / -port=).
	void ApplyServerPortCli(int argc, char** argv, engine::core::Config& config)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (!argv[i]) continue;
			std::string_view arg(argv[i]);
			// --port=3840 or -port=3840
			if ((arg.size() >= 6 && (arg.substr(0, 6) == "--port=" || arg.substr(0, 6) == "-port=")))
			{
				std::string_view value = arg.substr(6);
				char* end = nullptr;
				long v = std::strtol(value.data(), &end, 10);
				if (end != value.data() && v >= 1 && v <= 65535)
				{
					config.SetValue("server.listen_port", static_cast<int64_t>(v));
					return;
				}
			}
			// --port 3840 or -port 3840
			if ((arg == "--port" || arg == "-port") && i + 1 < argc && argv[i + 1])
			{
				char* end = nullptr;
				long v = std::strtol(argv[i + 1], &end, 10);
				if (end != argv[i + 1] && v >= 1 && v <= 65535)
				{
					config.SetValue("server.listen_port", static_cast<int64_t>(v));
					return;
				}
			}
		}
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
	ApplyServerPortCli(argc, argv, config);

	// Default log file for server includes "server" in the name (config key log.file, default lcdlln_server.log).
	engine::core::LogSettings logSettings;
	logSettings.level = ParseLogLevel(config.GetString("log.level", "Info"));
	logSettings.console = config.GetBool("log.console", true) || HasCliFlag(argc, argv, "-console");
	logSettings.flushAlways = true;
	logSettings.filePath = HasCliFlag(argc, argv, "-log")
		? engine::core::Log::MakeTimestampedFilename("lcdlln_server")
		: config.GetString("log.file", "lcdlln_server.log");
	logSettings.rotation_size_mb = static_cast<size_t>(std::max(0, config.GetInt("log.rotation_size_mb", 10)));
	logSettings.retention_days = static_cast<int>(config.GetInt("log.retention_days", 7));
	engine::core::Log::Init(logSettings);

	const uint16_t port = static_cast<uint16_t>(config.GetInt("server.listen_port", 27015));
	LOG_INFO(Net, "[Server] lcdlln_server starting — log file: {}, port: {} (UDP), console: {}",
		logSettings.filePath.empty() ? "<none>" : logSettings.filePath,
		port,
		logSettings.console ? "on" : "off");

	engine::server::ServerApp app(std::move(config));
	g_serverApp = &app;
	if (!SetConsoleCtrlHandler(HandleConsoleCtrl, TRUE))
	{
		LOG_WARN(Core, "[Server] SetConsoleCtrlHandler failed");
	}

	int result = 1;
	if (!app.Init())
	{
		LOG_ERROR(Net, "[Server] Init failed — check log file and console for errors");
	}
	else
	{
		LOG_INFO(Net, "[Server] Listening on UDP port {}. Ready for client connections.", port);
		result = app.Run();
	}

	app.Shutdown();
	g_serverApp = nullptr;
	LOG_INFO(Net, "[Server] Shutdown complete (exit code {})", result);
	engine::core::Log::Shutdown();
	return result;
}
