/// Minimal Linux TCP server entry point (M19.4). Uses NetServer (epoll); no game/DB logic.
/// M20.5: Auth/Register handlers wired via AuthRegisterHandler.

#include "engine/server/NetServer.h"
#include "engine/server/AuthRegisterHandler.h"
#include "engine/server/InMemoryAccountStore.h"
#include "engine/server/SessionManager.h"
#include "engine/server/RateLimitAndBan.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/server/ConnectionSessionMap.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <csignal>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>

namespace
{
	volatile sig_atomic_t g_quit = 0;
	bool g_net_stats = false;

	void OnSignal(int)
	{
		g_quit = 1;
	}

	/// Returns true if \a argv contains --net.stats (or -net.stats).
	bool ParseNetStatsFlag(int argc, char** argv)
	{
		for (int i = 1; i < argc; ++i)
			if (argv[i] != nullptr && (std::strcmp(argv[i], "--net.stats") == 0 || std::strcmp(argv[i], "-net.stats") == 0))
				return true;
		return false;
	}

	/// Logs a single line with network stats (throttled; do not call per packet).
	void LogNetworkStats(const engine::server::NetServerStats& s)
	{
		LOG_INFO(Net, "[NetServer] stats: conn_active={} conn_total={} handshake_ok={} handshake_fail={} bytes_in={} bytes_out={} pkt_in={} pkt_out={} pkt_dropped={}",
			s.connectionsActive, s.connectionsTotal, s.handshakeSuccess, s.handshakeFail,
			s.bytesIn, s.bytesOut, s.packetsIn, s.packetsOut, s.packetsDropped);
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);

	g_net_stats = ParseNetStatsFlag(argc, argv);

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
	netConfig.packetRatePerSec = static_cast<double>(config.GetInt("server.tcp.packet_rate_per_sec", 200));
	netConfig.packetBurst = static_cast<double>(config.GetInt("server.tcp.packet_burst", 400));
	netConfig.decodeFailureThreshold = static_cast<uint32_t>(config.GetInt("server.tcp.decode_failure_threshold", 5));
	netConfig.handshakeTimeoutSec = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_timeout_sec", 10));

	uint16_t port = static_cast<uint16_t>(config.GetInt("server.tcp.port", 3840));
	if (!server.Init(port, netConfig))
	{
		LOG_ERROR(Net, "[ServerMain] NetServer Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	engine::server::SessionManager sessionManager;
	sessionManager.SetConfig(engine::server::SessionManager::LoadConfig(config));
	LOG_INFO(Net, "[ServerMain] SessionManager configured");

	engine::server::RateLimitAndBan rateLimit;
	rateLimit.SetConfig(engine::server::RateLimitAndBan::LoadConfig(config));
	LOG_INFO(Net, "[ServerMain] RateLimitAndBan configured");

	engine::server::SecurityAuditLog auditLog;
	std::string auditPath = config.GetString("security.audit_log_path", "security_audit.log");
	if (auditLog.Init(auditPath))
		LOG_INFO(Net, "[ServerMain] SecurityAuditLog opened: {}", auditPath);
	else
		LOG_WARN(Net, "[ServerMain] SecurityAuditLog Init failed (path={})", auditPath);

	engine::server::InMemoryAccountStore accountStore;
	engine::server::AuthRegisterHandler authHandler;
	authHandler.SetServer(&server);
	authHandler.SetAccountStore(&accountStore);
	authHandler.SetSessionManager(&sessionManager);
	authHandler.SetRateLimitAndBan(&rateLimit);
	authHandler.SetSecurityAuditLog(&auditLog);
	engine::server::ConnectionSessionMap connSessionMap;
	authHandler.SetConnectionSessionMap(&connSessionMap);
	server.SetPacketHandler([&authHandler](uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize) {
		authHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
	});
	LOG_INFO(Net, "[ServerMain] Auth/Register and Heartbeat handler set (opcodes 1, 3, 7)");

	LOG_INFO(Net, "[ServerMain] NetServer running on port {} (Ctrl+C to stop)", port);

	auto lastStatsDump = std::chrono::steady_clock::now();
	constexpr auto kStatsInterval = std::chrono::seconds(10);

	auto lastWatchdog = std::chrono::steady_clock::now();
	constexpr auto kWatchdogInterval = std::chrono::seconds(10);

	while (server.IsRunning() && g_quit == 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		auto now = std::chrono::steady_clock::now();
		if (now - lastWatchdog >= kWatchdogInterval)
		{
			lastWatchdog = now;
			sessionManager.EvictExpired();
			auto expired = connSessionMap.CollectExpired(sessionManager);
			for (const auto& [connId, sessionId] : expired)
			{
				server.CloseConnection(connId, engine::server::DisconnectReason::HeartbeatTimeout);
				sessionManager.Close(sessionId, engine::server::SessionCloseReason::HeartbeatTimeout);
				LOG_INFO(Net, "[ServerMain] Session expired (connId={}, session_id={}), connection closed", connId, sessionId);
			}
		}

		if (g_net_stats)
		{
			auto now = std::chrono::steady_clock::now();
			if (now - lastStatsDump >= kStatsInterval)
			{
				engine::server::NetServerStats stats;
				server.GetNetworkStats(stats);
				LogNetworkStats(stats);
				lastStatsDump = now;
			}
		}
	}

	if (g_net_stats)
	{
		engine::server::NetServerStats stats;
		server.GetNetworkStats(stats);
		LOG_INFO(Net, "[ServerMain] Final network stats:");
		LogNetworkStats(stats);
		uint64_t totalDisconnects = 0;
		for (size_t i = 0; i < static_cast<size_t>(engine::server::DisconnectReason::Count); ++i)
			totalDisconnects += stats.disconnectByReason[i];
		if (totalDisconnects > 0)
			LOG_INFO(Net, "[ServerMain] Disconnect histogram: peer_closed={} EPOLLERR={} EPOLLHUP={} invalid_pkt={} decode_fail={} rate_limit={} handshake_timeout={} tls_fail={} ssl_r={} ssl_w={} recv={} send={} tx_cap={} heartbeat_timeout={}",
				stats.disconnectByReason[0], stats.disconnectByReason[1], stats.disconnectByReason[2], stats.disconnectByReason[3], stats.disconnectByReason[4],
				stats.disconnectByReason[5], stats.disconnectByReason[6], stats.disconnectByReason[7], stats.disconnectByReason[8], stats.disconnectByReason[9],
				stats.disconnectByReason[10], stats.disconnectByReason[11], stats.disconnectByReason[12], stats.disconnectByReason[13]);
	}

	server.Shutdown();
	LOG_INFO(Net, "[ServerMain] Shutdown complete");
	engine::core::Log::Shutdown();
	return 0;
}
