/// M22.6 — Shard server entry point: accepts client connections; first packet must be PRESENT_SHARD_TICKET; validates and responds TICKET_ACCEPTED/REJECTED.

#include "engine/server/HealthEndpoint.h"
#include "engine/server/PrometheusMetrics.h"
#include "engine/server/NetServer.h"
#include "engine/server/ShardTicketValidator.h"
#include "engine/server/ShardTicketHandshakeHandler.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <csignal>
#include <chrono>
#include <algorithm>
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
	double capKBps = config.GetDouble("server.bandwidth.max_bandwidth_per_player", -1.0);
	if (capKBps <= 0.0)
		capKBps = config.GetDouble("server.max_bandwidth_per_player", -1.0);
	if (capKBps <= 0.0)
	{
		const double derivedKBps = netConfig.packetRatePerSec
			* static_cast<double>(engine::network::kProtocolV1MaxPacketSize) / 1024.0;
		capKBps = std::max(derivedKBps, 1.0);
		LOG_WARN(Net,
			"[ShardMain] max_bandwidth_per_player missing -> using derived default {} KB/s",
			capKBps);
	}
	netConfig.maxBandwidthPerPlayerBytesPerSec = capKBps * 1024.0;
	LOG_INFO(Net,
		"[ShardMain] TX bandwidth cap per player (bytes/sec={})",
		static_cast<uint64_t>(netConfig.maxBandwidthPerPlayerBytesPerSec));
	netConfig.decodeFailureThreshold = static_cast<uint32_t>(config.GetInt("server.tcp.decode_failure_threshold", 5));
	netConfig.handshakeTimeoutSec = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_timeout_sec", 10));
	netConfig.maxConnectionsPerIp = static_cast<uint32_t>(config.GetInt("server.tcp.max_connections_per_ip", 20));
	netConfig.maxAcceptsPerSec = config.GetDouble("server.tcp.accept_throttle_max_accepts_per_sec", 200.0);
	netConfig.handshakeFailuresBeforeDeny = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_failures_before_deny", 5));
	netConfig.handshakeDenyDurationSec = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_deny_duration_sec", 60));
	LOG_INFO(Net,
		"[ShardMain] DDoS connection throttle cfg (maxConnectionsPerIp={} maxAcceptsPerSec={} handshakeFailuresBeforeDeny={} handshakeDenyDurationSec={})",
		netConfig.maxConnectionsPerIp, netConfig.maxAcceptsPerSec,
		netConfig.handshakeFailuresBeforeDeny, netConfig.handshakeDenyDurationSec);

	uint16_t port = static_cast<uint16_t>(config.GetInt("shard.port", 3841));
	if (!server.Init(port, netConfig))
	{
		LOG_ERROR(Net, "[ShardMain] NetServer Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}
	LOG_INFO(Net, "[ShardMain] NetServer listening on port {}", port);

	// M23.1 + M23.2 — Health/readiness and Prometheus /metrics (Shard: no auth/sessions/shard registry/DB).
	engine::server::HealthEndpoint healthEndpoint;
	uint16_t healthPort = static_cast<uint16_t>(config.GetInt("server.health.port", 3843));
	std::string healthBind = config.GetString("server.health.bind", "127.0.0.1");
	auto metricsProvider = [&server]() {
		engine::server::NetServerStats netStats;
		server.GetNetworkStats(netStats);
		return engine::server::BuildPrometheusText(netStats, 0, 0, 0, 0, nullptr);
	};
	if (healthEndpoint.Init(healthPort, healthBind, []() { return true; }, metricsProvider))
		LOG_INFO(Net, "[ShardMain] Health endpoint listening on {}:{} (/healthz, /readyz, /metrics)", healthBind, healthPort);
	else
		LOG_WARN(Net, "[ShardMain] Health endpoint Init failed (port {}), continuing without health endpoint", healthPort);

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
	healthEndpoint.Shutdown();
	engine::core::Log::Shutdown();
	return 0;
}
