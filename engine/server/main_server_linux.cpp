/// Minimal Linux TCP server entry point (M19.4). Uses NetServer (epoll); no game/DB logic.
/// M20.5: Auth/Register handlers wired via AuthRegisterHandler.

#include "engine/server/MigrationRunner.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"
#include "engine/server/HealthEndpoint.h"
#include "engine/server/PrometheusMetrics.h"
#include "engine/server/NetServer.h"
#include "engine/server/ShardRegisterHandler.h"
#include "engine/server/ShardRegistry.h"
#include "engine/server/ShardTicketHandler.h"
#include "engine/server/ServerListHandler.h"
#include "engine/network/ProtocolV1Constants.h"
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
#include <cstdio>
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
	std::fprintf(stderr, "[MAIN_SRV] boot start\n"); std::fflush(stderr);
	std::fprintf(stderr, "[MAIN_SRV] avant config load\n"); std::fflush(stderr);
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);

	g_net_stats = ParseNetStatsFlag(argc, argv);

	engine::core::LogSettings logSettings;
	logSettings.level = ParseLogLevel(config.GetString("log.level", "Info"));
	logSettings.console = true;
	logSettings.flushAlways = true;
	logSettings.filePath = config.GetString("log.file", "engine.log");
	logSettings.rotation_size_mb = static_cast<size_t>(std::max(0, config.GetInt("log.rotation_size_mb", 10)));
	logSettings.retention_days = static_cast<int>(config.GetInt("log.retention_days", 7));
	engine::core::Log::Init(logSettings);

	LOG_INFO(Net, "[ServerMain] Linux TCP server starting...");

	if (!engine::server::MigrationRunnerRun(config))
	{
		LOG_ERROR(Core, "[ServerMain] Migrations failed, exiting");
		engine::core::Log::Shutdown();
		return 1;
	}

	engine::server::db::ConnectionPool dbPool;
	dbPool.Init(config);

	// M23.2 — DB query latency histogram for Prometheus. Observer set before any DB use.
	engine::server::DbLatencyHistogram dbLatencyHistogram;
	engine::server::db::SetDbLatencyObserver([&dbLatencyHistogram](int ms) { dbLatencyHistogram.Observe(ms); });

	std::fprintf(stderr, "[MAIN_SRV] avant ShardRegistry setup\n"); std::fflush(stderr);
	engine::server::ShardRegistry shardRegistry;
	engine::server::ShardRegisterHandler shardRegisterHandler;
	shardRegisterHandler.SetShardRegistry(&shardRegistry);
	std::fprintf(stderr, "[MAIN_SRV] ShardRegistry setup OK\n"); std::fflush(stderr);

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
	std::fprintf(stderr, "[MAIN_SRV] config OK port=%u\n", static_cast<unsigned>(port)); std::fflush(stderr);
	std::fprintf(stderr, "[MAIN_SRV] avant NetServer::Init port=%u\n", static_cast<unsigned>(port)); std::fflush(stderr);
	bool initOk = server.Init(port, netConfig);
	std::fprintf(stderr, "[MAIN_SRV] NetServer::Init r=%d\n", (int)initOk); std::fflush(stderr);
	if (!initOk)
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
	size_t rotationMb = static_cast<size_t>(std::max(0, config.GetInt("log.rotation_size_mb", 10)));
	int retentionDays = static_cast<int>(config.GetInt("log.retention_days", 7));
	if (auditLog.Init(auditPath, rotationMb, retentionDays))
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
	shardRegisterHandler.SetServer(&server);
	engine::server::ShardTicketHandler shardTicketHandler;
	shardTicketHandler.SetServer(&server);
	shardTicketHandler.SetShardRegistry(&shardRegistry);
	shardTicketHandler.SetSessionManager(&sessionManager);
	shardTicketHandler.SetConnectionSessionMap(&connSessionMap);
	shardTicketHandler.SetSecret(config.GetString("shard.ticket_hmac_secret", ""));
	shardTicketHandler.SetValiditySec(static_cast<int>(config.GetInt("shard.ticket_validity_sec", 60)));
	engine::server::ServerListHandler serverListHandler;
	serverListHandler.SetServer(&server);
	serverListHandler.SetShardRegistry(&shardRegistry);
	std::fprintf(stderr, "[MAIN_SRV] avant SetPacketHandler\n"); std::fflush(stderr);
	server.SetPacketHandler([&authHandler, &shardRegisterHandler, &shardTicketHandler, &serverListHandler](uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize) {
		if (opcode == engine::network::kOpcodeShardRegister || opcode == engine::network::kOpcodeShardHeartbeat)
			shardRegisterHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == engine::network::kOpcodeRequestShardTicket)
			shardTicketHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == engine::network::kOpcodeServerListRequest)
			serverListHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else
			authHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
	});

	// M23.1 + M23.2 — Health/readiness and Prometheus /metrics on same port.
	engine::server::HealthEndpoint healthEndpoint;
	uint16_t healthPort = static_cast<uint16_t>(config.GetInt("server.health.port", 3842));
	std::string healthBind = config.GetString("server.health.bind", "127.0.0.1");
	auto readyCheck = [&dbPool]() {
		auto guard = dbPool.Acquire();
		return guard.get() != nullptr;
	};
	auto metricsProvider = [&server, &sessionManager, &shardRegistry, &authHandler, &dbLatencyHistogram]() {
		engine::server::NetServerStats netStats;
		server.GetNetworkStats(netStats);
		uint64_t sessionsActive = sessionManager.GetActiveCount();
		auto shards = shardRegistry.ListShards();
		uint64_t shardsOnline = 0;
		for (const auto& s : shards)
		{
			if (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded)
				++shardsOnline;
		}
		return engine::server::BuildPrometheusText(netStats, sessionsActive, shardsOnline,
			authHandler.GetAuthSuccessTotal(), authHandler.GetAuthFailTotal(), &dbLatencyHistogram);
	};
	if (healthEndpoint.Init(healthPort, healthBind, readyCheck, metricsProvider))
		LOG_INFO(Net, "[ServerMain] Health endpoint listening on {}:{} (/healthz, /readyz, /metrics)", healthBind, healthPort);
	else
		LOG_WARN(Net, "[ServerMain] Health endpoint Init failed (port {}), continuing without health endpoint", healthPort);

	std::fprintf(stderr, "[MAIN_SRV] SetPacketHandler OK\n"); std::fflush(stderr);
	int shardHeartbeatTimeoutSec = static_cast<int>(config.GetInt("shard.heartbeat_timeout_sec", 90));
	shardRegistry.SetShardDownCallback([](uint32_t shard_id) {
		LOG_INFO(Net, "[ServerMain] Shard down event: shard_id={}", shard_id);
	});
	double degradedLoadThreshold = config.GetDouble("shard.degraded_load_threshold", 0.90);
	shardRegistry.SetDegradedLoadThreshold(degradedLoadThreshold);
	shardRegistry.SetShardDegradedCallback([](uint32_t shard_id) {
		LOG_INFO(Net, "[ServerMain] Shard degraded event: shard_id={}", shard_id);
	});
	LOG_INFO(Net, "[ServerMain] Auth/Register/Heartbeat/ShardTicket/ServerList and Shard register handler set (opcodes 1, 3, 7, 10, 13, 14, 19)");

	LOG_INFO(Net, "[ServerMain] NetServer running on port {} (Ctrl+C to stop)", port);

	std::fprintf(stderr, "[MAIN_SRV] entering main loop\n"); std::fflush(stderr);
	auto lastStatsDump = std::chrono::steady_clock::now();
	constexpr auto kStatsInterval = std::chrono::seconds(10);

	auto lastWatchdog = std::chrono::steady_clock::now();
	constexpr auto kWatchdogInterval = std::chrono::seconds(10);

	// M23.3 — Logs résumés état cluster (throttled 60s).
	auto lastSummaryLog = std::chrono::steady_clock::now();
	constexpr auto kSummaryInterval = std::chrono::seconds(60);

	while (server.IsRunning() && g_quit == 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		auto now = std::chrono::steady_clock::now();
		if (now - lastSummaryLog >= kSummaryInterval)
		{
			lastSummaryLog = now;
			engine::server::NetServerStats sumStats;
			server.GetNetworkStats(sumStats);
			uint64_t sumSessions = sessionManager.GetActiveCount();
			auto sumShards = shardRegistry.ListShards();
			uint64_t sumShardsOnline = 0;
			for (const auto& s : sumShards)
			{
				if (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded)
					++sumShardsOnline;
			}
			bool dbOk = readyCheck();
			LOG_INFO(Net, "[ServerMain] cluster summary: conn_active={} sessions_active={} shards_online={} auth_success={} auth_fail={} db_ok={}",
				sumStats.connectionsActive, sumSessions, sumShardsOnline,
				authHandler.GetAuthSuccessTotal(), authHandler.GetAuthFailTotal(), dbOk ? 1 : 0);
		}

		if (now - lastWatchdog >= kWatchdogInterval)
		{
			lastWatchdog = now;
			sessionManager.EvictExpired();
			std::fprintf(stderr, "[MAIN_SRV] EvictStaleHeartbeats timeout=%d\n", shardHeartbeatTimeoutSec); std::fflush(stderr);
			shardRegistry.EvictStaleHeartbeats(shardHeartbeatTimeoutSec);
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

	std::fprintf(stderr, "[MAIN_SRV] main loop exited, avant Shutdown\n"); std::fflush(stderr);
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
	std::fprintf(stderr, "[MAIN_SRV] NetServer::Shutdown OK\n"); std::fflush(stderr);
	healthEndpoint.Shutdown();
	dbPool.Shutdown();
	LOG_INFO(Net, "[ServerMain] Shutdown complete");
	std::fprintf(stderr, "[MAIN_SRV] shutdown complete\n"); std::fflush(stderr);
	engine::core::Log::Shutdown();
	return 0;
}
