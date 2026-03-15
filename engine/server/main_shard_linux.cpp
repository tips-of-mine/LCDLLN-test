/// M22.6 — Shard server entry point: accepts client connections; first packet must be PRESENT_SHARD_TICKET; validates and responds TICKET_ACCEPTED/REJECTED.

#include "engine/server/NetServer.h"
#include "engine/server/ShardTicketValidator.h"
#include "engine/server/ShardTicketHandshakeHandler.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <csignal>
#include <chrono>
#include <thread>

namespace
{
	volatile sig_atomic_t g_quit = 0;
	void OnSignal(int) { g_quit = 1; }
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	logSettings.flushAlways = true;
	logSettings.filePath = config.GetString("log.file", "shard.log");
	engine::core::Log::Init(logSettings);

	LOG_INFO(Net, "[ShardMain] Shard server starting...");

	engine::server::NetServer server;
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);

	engine::server::NetServerConfig netConfig;
	netConfig.maxConnections = static_cast<uint32_t>(config.GetInt("server.tcp.max_connections", 1000));
	netConfig.maxQueuedTxBytesPerConnection = static_cast<size_t>(config.GetInt("server.tcp.max_queued_tx_bytes", 262144));
	netConfig.workerThreadCount = static_cast<uint32_t>(config.GetInt("server.tcp.worker_threads", 4));
	netConfig.tlsCertPath = config.GetString("server.tls.cert", "");
	netConfig.tlsKeyPath = config.GetString("server.tls.key", "");
	netConfig.packetRatePerSec = static_cast<double>(config.GetInt("server.tcp.packet_rate_per_sec", 200));
	netConfig.packetBurst = static_cast<double>(config.GetInt("server.tcp.packet_burst", 400));
	netConfig.decodeFailureThreshold = static_cast<uint32_t>(config.GetInt("server.tcp.decode_failure_threshold", 5));
	netConfig.handshakeTimeoutSec = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_timeout_sec", 10));

	uint16_t port = static_cast<uint16_t>(config.GetInt("shard.port", 3841));
	if (!server.Init(port, netConfig))
	{
		LOG_ERROR(Net, "[ShardMain] NetServer Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}
	LOG_INFO(Net, "[ShardMain] NetServer listening on port {}", port);

	engine::server::ShardTicketValidator validator;
	validator.SetSecret(config.GetString("shard.ticket_hmac_secret", ""));
	validator.SetShardId(static_cast<uint32_t>(config.GetInt("shard.id", 1)));

	engine::server::ShardTicketHandshakeHandler handshakeHandler;
	handshakeHandler.SetServer(&server);
	handshakeHandler.SetValidator(&validator);
	server.SetPacketHandler([&handshakeHandler](uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionId,
		const uint8_t* payload, size_t payloadSize) {
		handshakeHandler.HandlePacket(connId, opcode, requestId, sessionId, payload, payloadSize);
	});

	LOG_INFO(Net, "[ShardMain] Shard server running (Ctrl+C to stop)");

	while (server.IsRunning() && g_quit == 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

	LOG_INFO(Net, "[ShardMain] Shutdown");
	engine::core::Log::Shutdown();
	return 0;
}
