/// Minimal Linux TCP server entry point (M19.4). Uses NetServer (epoll); no game/DB logic.
/// Build and run on Linux to validate NetServer (e.g. 1000 connexions stables).

#include "engine/server/NetServer.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <csignal>
#include <chrono>
#include <string_view>
#include <thread>

namespace
{
	volatile sig_atomic_t g_quit = 0;

	void OnSignal(int)
	{
		g_quit = 1;
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);

	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	logSettings.flushAlways = true;
	logSettings.filePath = config.GetString("log.file", "engine.log");
	engine::core::Log::Init(logSettings);

	LOG_INFO(Net, "[ServerMain] Linux TCP server starting...");

	engine::server::NetServer server;
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);

	engine::server::NetServerConfig netConfig;
	netConfig.maxConnections = static_cast<uint32_t>(config.GetInt("server.tcp.max_connections", 1000));
	netConfig.maxQueuedTxBytesPerConnection = static_cast<size_t>(config.GetInt("server.tcp.max_queued_tx_bytes", 262144));
	netConfig.workerThreadCount = static_cast<uint32_t>(config.GetInt("server.tcp.worker_threads", 4));
	netConfig.tlsCertPath = config.GetString("server.tls.cert", "");
	netConfig.tlsKeyPath = config.GetString("server.tls.key", "");

	uint16_t port = static_cast<uint16_t>(config.GetInt("server.tcp.port", 3840));
	if (!server.Init(port, netConfig))
	{
		LOG_ERROR(Net, "[ServerMain] NetServer Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	LOG_INFO(Net, "[ServerMain] NetServer running on port {} (Ctrl+C to stop)", port);

	while (server.IsRunning() && g_quit == 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

	server.Shutdown();
	LOG_INFO(Net, "[ServerMain] Shutdown complete");
	engine::core::Log::Shutdown();
	return 0;
}
