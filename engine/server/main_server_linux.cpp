/// Minimal Linux TCP server entry point (M19.4). Uses NetServer (epoll); no game/DB logic.
/// M20.5: Auth/Register handlers wired via AuthRegisterHandler.
/// M33.2: PasswordResetHandler wired for password reset + email verification opcodes.

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
#include "engine/server/AccountStore.h"
#include "engine/server/InMemoryAccountStore.h"
#include "engine/server/MysqlAccountStore.h"
#include "engine/server/SessionManager.h"
#include "engine/server/RateLimitAndBan.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/server/ConnectionSessionMap.h"
#include "engine/server/PasswordResetStore.h"
#include "engine/server/PasswordResetHandler.h"
#include "engine/server/SmtpMailer.h"
#include "engine/server/TermsRepository.h"
#include "engine/server/TermsHandler.h"
#include "engine/server/CharacterCreateHandler.h"

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
	LOG_DEBUG(Server, "[MAIN_SRV] boot start");
	LOG_DEBUG(Server, "[MAIN_SRV] avant config load");
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);

	g_net_stats = ParseNetStatsFlag(argc, argv);

	engine::core::LogSettings logSettings;
	logSettings.level = ParseLogLevel(config.GetString("log.level", "Info"));
	logSettings.console = true;
	logSettings.flushAlways = true;
	logSettings.filePath = config.GetString("log.file", "engine.log");
	logSettings.rotation_size_mb = static_cast<size_t>(std::max(static_cast<int64_t>(0), config.GetInt("log.rotation_size_mb", 10)));
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

	LOG_DEBUG(Server, "[MAIN_SRV] avant ShardRegistry setup");
	engine::server::ShardRegistry shardRegistry;
	engine::server::ShardRegisterHandler shardRegisterHandler;
	shardRegisterHandler.SetShardRegistry(&shardRegistry);
	LOG_INFO(Server, "[MAIN_SRV] ShardRegistry setup OK");

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
		// Default derived from current packet-rate limit: worst-case bytes/sec.
		const double derivedKBps = netConfig.packetRatePerSec
			* static_cast<double>(engine::network::kProtocolV1MaxPacketSize) / 1024.0;
		capKBps = std::max(derivedKBps, 1.0);
		LOG_WARN(Net,
			"[ServerMain] max_bandwidth_per_player missing -> using derived default {} KB/s",
			capKBps);
	}
	netConfig.maxBandwidthPerPlayerBytesPerSec = capKBps * 1024.0;
	LOG_INFO(Net,
		"[ServerMain] TX bandwidth cap per player (bytes/sec={})",
		static_cast<uint64_t>(netConfig.maxBandwidthPerPlayerBytesPerSec));
	netConfig.decodeFailureThreshold = static_cast<uint32_t>(config.GetInt("server.tcp.decode_failure_threshold", 5));
	netConfig.handshakeTimeoutSec = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_timeout_sec", 10));
	netConfig.maxConnectionsPerIp = static_cast<uint32_t>(config.GetInt("server.tcp.max_connections_per_ip", 20));
	netConfig.maxAcceptsPerSec = config.GetDouble("server.tcp.accept_throttle_max_accepts_per_sec", 200.0);
	netConfig.handshakeFailuresBeforeDeny = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_failures_before_deny", 5));
	netConfig.handshakeDenyDurationSec = static_cast<uint32_t>(config.GetInt("server.tcp.handshake_deny_duration_sec", 60));
	LOG_INFO(Net,
		"[ServerMain] DDoS connection throttle cfg (maxConnectionsPerIp={} maxAcceptsPerSec={} handshakeFailuresBeforeDeny={} handshakeDenyDurationSec={})",
		netConfig.maxConnectionsPerIp, netConfig.maxAcceptsPerSec,
		netConfig.handshakeFailuresBeforeDeny, netConfig.handshakeDenyDurationSec);

	uint16_t port = static_cast<uint16_t>(config.GetInt("server.tcp.port", 3840));
	LOG_INFO(Server, "[MAIN_SRV] config OK port={}", static_cast<unsigned>(port));
	LOG_INFO(Server, "[MAIN_SRV] avant NetServer::Init port={}", static_cast<unsigned>(port));
	bool initOk = server.Init(port, netConfig);
	LOG_INFO(Server, "[MAIN_SRV] NetServer::Init r={}", (int)initOk);
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
	size_t rotationMb = static_cast<size_t>(std::max(static_cast<int64_t>(0), config.GetInt("log.rotation_size_mb", 10)));
	int retentionDays = static_cast<int>(config.GetInt("log.retention_days", 7));
	if (auditLog.Init(auditPath, rotationMb, retentionDays))
		LOG_INFO(Net, "[ServerMain] SecurityAuditLog opened: {}", auditPath);
	else
		LOG_WARN(Net, "[ServerMain] SecurityAuditLog Init failed (path={})", auditPath);

	// M33.2: SMTP config + password reset / email verification stores.
	engine::server::SmtpConfig smtpConfig = engine::server::SmtpConfig::Load(config, "config.json");
	engine::server::PasswordResetStore passwordResetStore;
	engine::server::PasswordResetHandler passwordResetHandler;

	engine::server::InMemoryAccountStore accountStoreMem;
	engine::server::MysqlAccountStore accountStoreMysql(&dbPool);
	engine::server::AccountStore* accountStore = nullptr;
	if (dbPool.IsInitialized())
	{
		accountStore = &accountStoreMysql;
		LOG_INFO(Auth, "[ServerMain] AccountStore: MySQL (accounts persistés)");
	}
	else
	{
		accountStore = &accountStoreMem;
		LOG_WARN(Auth, "[ServerMain] AccountStore: mémoire uniquement (pool MySQL indisponible — comptes non persistés)");
	}
	engine::server::AuthRegisterHandler authHandler;
	authHandler.SetServer(&server);
	authHandler.SetAccountStore(accountStore);
	authHandler.SetSessionManager(&sessionManager);
	authHandler.SetRateLimitAndBan(&rateLimit);
	authHandler.SetSecurityAuditLog(&auditLog);
	authHandler.SetPasswordResetStore(&passwordResetStore);
	authHandler.SetSmtpConfig(&smtpConfig);
	engine::server::ConnectionSessionMap connSessionMap;
	authHandler.SetConnectionSessionMap(&connSessionMap);

	engine::server::TermsRepository termsRepository;
	termsRepository.Init(config, &dbPool);
	engine::server::TermsHandler termsHandler;
	termsHandler.SetServer(&server);
	termsHandler.SetSessionManager(&sessionManager);
	termsHandler.SetConnectionSessionMap(&connSessionMap);
	termsHandler.SetAccountStore(accountStore);
	termsHandler.SetTermsRepository(&termsRepository);
	termsHandler.SetSmtpConfig(&smtpConfig);

	engine::server::CharacterCreateHandler characterCreateHandler;
	characterCreateHandler.SetServer(&server);
	characterCreateHandler.SetSessionManager(&sessionManager);
	characterCreateHandler.SetConnectionSessionMap(&connSessionMap);
	characterCreateHandler.SetConnectionPool(&dbPool);
	characterCreateHandler.SetConfig(&config);

	// Wire PasswordResetHandler dependencies.
	passwordResetHandler.SetServer(&server);
	passwordResetHandler.SetAccountStore(accountStore);
	passwordResetHandler.SetPasswordResetStore(&passwordResetStore);
	passwordResetHandler.SetSmtpConfig(&smtpConfig);
	passwordResetHandler.SetRateLimitAndBan(&rateLimit);
	passwordResetHandler.SetSecurityAuditLog(&auditLog);
	LOG_INFO(Auth, "[ServerMain] PasswordResetHandler configured (M33.2)");
	shardRegisterHandler.SetServer(&server);
	engine::server::ShardTicketHandler shardTicketHandler;
	shardTicketHandler.SetServer(&server);
	shardTicketHandler.SetShardRegistry(&shardRegistry);
	shardTicketHandler.SetSessionManager(&sessionManager);
	shardTicketHandler.SetConnectionSessionMap(&connSessionMap);
	shardTicketHandler.SetAccountStore(accountStore);
	shardTicketHandler.SetTermsRepository(&termsRepository);
	shardTicketHandler.SetSecret(config.GetString("shard.ticket_hmac_secret", ""));
	shardTicketHandler.SetValiditySec(static_cast<int>(config.GetInt("shard.ticket_validity_sec", 60)));
	engine::server::ServerListHandler serverListHandler;
	serverListHandler.SetServer(&server);
	serverListHandler.SetShardRegistry(&shardRegistry);
	LOG_DEBUG(Server, "[MAIN_SRV] avant SetPacketHandler");
	server.SetPacketHandler([&authHandler, &shardRegisterHandler, &shardTicketHandler, &serverListHandler, &passwordResetHandler, &termsHandler, &characterCreateHandler](uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize) {
		using namespace engine::network;
		if (opcode == kOpcodeShardRegister || opcode == kOpcodeShardHeartbeat)
			shardRegisterHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeRequestShardTicket)
			shardTicketHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeServerListRequest)
			serverListHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeForgotPasswordRequest
		      || opcode == kOpcodeResetPasswordRequest
		      || opcode == kOpcodeVerifyEmailRequest)
			passwordResetHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeTermsStatusRequest || opcode == kOpcodeTermsContentRequest || opcode == kOpcodeTermsAcceptRequest)
			termsHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeCharacterCreateRequest)
			characterCreateHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
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
	auto statusProvider = [&]() -> std::string {
		const bool dbOk = readyCheck();
		const auto shards = shardRegistry.ListShards();

		auto escapeJson = [](std::string_view s) -> std::string {
			std::string out;
			out.reserve(s.size() + 8);
			for (unsigned char c : s)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20u)
					{
						out += "\\u00";
						const char hex[] = "0123456789ABCDEF";
						out += hex[(c >> 4) & 0xF];
						out += hex[c & 0xF];
					}
					else
					{
						out.push_back(static_cast<char>(c));
					}
					break;
				}
			}
			return out;
		};

		uint64_t totalPlayers = 0;
		uint32_t shardsOnline = 0;
		for (const auto& s : shards)
		{
			const bool ok = (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded);
			if (ok)
			{
				++shardsOnline;
				totalPlayers += s.current_load;
			}
		}

		const bool authOk = dbOk;
		const bool masterOk = dbOk && shardsOnline > 0;

		std::string serversJson;
		serversJson.reserve(shards.size() * 64u + 32u);

		auto appendServer = [&](std::string_view name, bool ok, uint32_t players,
			std::string_view endpoint, std::string_view region, uint32_t maxCapacity, engine::server::ShardState state) {
			if (!serversJson.empty())
				serversJson += ",";
			serversJson += "{";
			serversJson += "\"name\":\"";
			serversJson += escapeJson(name);
			serversJson += "\",";
			serversJson += "\"ok\":";
			serversJson += (ok ? "true" : "false");
			serversJson += ",";
			serversJson += "\"players\":";
			serversJson += std::to_string(players);
			serversJson += ",";
			serversJson += "\"max_capacity\":";
			serversJson += std::to_string(maxCapacity);
			serversJson += ",";
			serversJson += "\"endpoint\":\"";
			serversJson += escapeJson(endpoint);
			serversJson += "\",";
			serversJson += "\"region\":\"";
			serversJson += escapeJson(region);
			serversJson += "\",";
			serversJson += "\"state\":";
			serversJson += std::to_string(static_cast<int>(state));
			serversJson += "}";
		};

		if (shards.empty())
		{
			appendServer("NO_SHARD", false, 0u, "", "", 0u, engine::server::ShardState::Offline);
		}
		else
		{
			for (const auto& s : shards)
			{
				const bool ok = (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded);
				appendServer(s.name, ok, s.current_load, s.endpoint, s.region, s.max_capacity, s.state);
			}
		}

		return std::string("{")
			+ "\"auth\":{"
			+ "\"ok\":" + (authOk ? "true" : "false")
			+ "},"
			+ "\"master\":{"
			+ "\"ok\":" + (masterOk ? "true" : "false")
			+ "},"
			+ "\"totalPlayers\":" + std::to_string(totalPlayers) + ","
			+ "\"game_servers\":["
			+ serversJson
			+ "]}";
	};

	auto webPortalStatusHtmlProvider = [&]() -> std::string {
		const bool dbOk = readyCheck();
		const auto shards = shardRegistry.ListShards();

		uint64_t totalPlayers = 0;
		uint32_t onlineServers = 0;
		for (const auto& s : shards)
		{
			const bool ok = (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded);
			if (ok)
			{
				++onlineServers;
				totalPlayers += s.current_load;
			}
		}

		const bool authOk = dbOk;
		const bool masterOk = dbOk && onlineServers > 0;

		const char* statusColorAuth = authOk ? "#35c759" : "#ff3b30";
		const char* statusColorMaster = masterOk ? "#35c759" : "#ff3b30";

		// Page publique: pas de tokens, pas d'IP, pas d'identifiants ni de logs.
		std::string html;
		html.reserve(1024);
		html += "<!doctype html><html><head><meta charset=\"utf-8\"/>";
		html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>";
		html += "<title>LCDLLN - Status</title>";
		html += "<style>body{font-family:system-ui,Segoe UI,Roboto,Arial,sans-serif;margin:24px}h1{font-size:20px}"
			".card{padding:16px;border:1px solid #ddd;border-radius:12px;margin-top:12px;max-width:520px}"
			".row{display:flex;gap:12px;align-items:baseline;justify-content:space-between;margin:6px 0}"
			".badge{font-weight:700;padding:2px 10px;border-radius:999px;color:#fff}"
			"</style></head><body>";
		html += "<h1>LCDLLN - Statut des services</h1>";
		html += "<div class=\"card\">";

		html += "<div class=\"row\"><span>Auth</span><span class=\"badge\" style=\"background-color:";
		html += statusColorAuth;
		html += ";\">";
		html += (authOk ? "OK" : "KO");
		html += "</span></div>";

		html += "<div class=\"row\"><span>Master</span><span class=\"badge\" style=\"background-color:";
		html += statusColorMaster;
		html += ";\">";
		html += (masterOk ? "OK" : "KO");
		html += "</span></div>";

		html += "<div class=\"row\"><span>Serveurs en ligne</span><span>";
		html += std::to_string(onlineServers);
		html += "</span></div>";

		html += "<div class=\"row\"><span>Joueurs (estimation)</span><span>";
		html += std::to_string(totalPlayers);
		html += "</span></div>";

		html += "</div>";
		html += "<p style=\"color:#666;margin-top:18px\">Données agrégées uniquement (aucune IP / token).</p>";
		html += "</body></html>";
		return html;
	};

	if (healthEndpoint.Init(healthPort, healthBind, readyCheck, metricsProvider, statusProvider, webPortalStatusHtmlProvider))
		LOG_INFO(Net, "[ServerMain] Health endpoint listening on {}:{} (/healthz, /readyz, /metrics)", healthBind, healthPort);
	else
		LOG_WARN(Net, "[ServerMain] Health endpoint Init failed (port {}), continuing without health endpoint", healthPort);

	LOG_INFO(Server, "[MAIN_SRV] SetPacketHandler OK");
	int shardHeartbeatTimeoutSec = static_cast<int>(config.GetInt("shard.heartbeat_timeout_sec", 90));
	shardRegistry.SetShardDownCallback([](uint32_t shard_id) {
		LOG_INFO(Net, "[ServerMain] Shard down event: shard_id={}", shard_id);
	});
	double degradedLoadThreshold = config.GetDouble("shard.degraded_load_threshold", 0.90);
	shardRegistry.SetDegradedLoadThreshold(degradedLoadThreshold);
	shardRegistry.SetShardDegradedCallback([](uint32_t shard_id) {
		LOG_INFO(Net, "[ServerMain] Shard degraded event: shard_id={}", shard_id);
	});
	LOG_INFO(Net, "[ServerMain] Handlers set: Auth/Register(1,3,7) Shard(10,13,14) ServerList(19) PasswordReset(21,23,25) Terms(27,29,31)");

	LOG_INFO(Net, "[ServerMain] NetServer running on port {} (Ctrl+C to stop)", port);

	LOG_DEBUG(Server, "[MAIN_SRV] entering main loop");
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
			const char* shardHint = (sumShardsOnline == 0)
				? " | shards: aucun shard jeu connecté (attendu tant qu’aucun serveur de monde ne s’enregistre)"
				: "";
			LOG_INFO(Net, "[ServerMain] cluster summary: conn_active={} sessions_active={} shards_online={} auth_success={} auth_fail={} db_ok={}{}",
				sumStats.connectionsActive, sumSessions, sumShardsOnline,
				authHandler.GetAuthSuccessTotal(), authHandler.GetAuthFailTotal(), dbOk ? 1 : 0, shardHint);
		}

		if (now - lastWatchdog >= kWatchdogInterval)
		{
			lastWatchdog = now;
			sessionManager.EvictExpired();
			LOG_DEBUG(Server, "[MAIN_SRV] EvictStaleHeartbeats timeout={}", shardHeartbeatTimeoutSec);
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

	LOG_DEBUG(Server, "[MAIN_SRV] main loop exited, avant Shutdow");
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
	LOG_INFO(Server, "[MAIN_SRV] NetServer::Shutdown OK");
	healthEndpoint.Shutdown();
	dbPool.Shutdown();
	LOG_INFO(Net, "[ServerMain] Shutdown complete");
	LOG_INFO(Server, "[MAIN_SRV] shutdown complete");
	engine::core::Log::Shutdown();
	return 0;
}
