/// Minimal Linux TCP server entry point (M19.4). Uses NetServer (epoll); no game/DB logic.
/// M20.5: Auth/Register handlers wired via AuthRegisterHandler.
/// M33.2: PasswordResetHandler wired for password reset + email verification opcodes.

#include "src/masterd/migrations/MigrationRunner.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/masterd/metrics/HealthEndpoint.h"
#include "src/masterd/metrics/PrometheusMetrics.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/shards/ServerRegistry.h"
#include "src/masterd/handlers/shard/ShardRegisterHandler.h"
#include "src/masterd/shards/ShardRegistry.h"
#include "src/masterd/handlers/shard/ShardTicketHandler.h"
#include "src/masterd/handlers/shard/ServerListHandler.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/masterd/handlers/auth/AuthRegisterHandler.h"
#include "src/masterd/account/AccountStore.h"
#include "src/masterd/account/InMemoryAccountStore.h"
#include "src/masterd/account/MysqlAccountStore.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/security/RateLimitAndBan.h"
#include "src/shared/security/SecurityAuditLog.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/handlers/password/PasswordResetStore.h"
#include "src/masterd/handlers/password/PasswordResetHandler.h"
#include "src/masterd/email/SmtpMailer.h"
#include "src/masterd/email/LocalizedEmail.h"
#include "src/masterd/handlers/terms/TermsRepository.h"
#include "src/masterd/handlers/terms/TermsHandler.h"
#include "src/masterd/handlers/character/CharacterCreateHandler.h"
#include "src/masterd/handlers/character/CharacterListHandler.h"
#include "src/masterd/handlers/character/CharacterDeleteHandler.h"
#include "src/masterd/handlers/character/CharacterSavePositionHandler.h"
#include "src/masterd/handlers/character/CharacterEnterWorldHandler.h"
#include "src/masterd/handlers/chat/ChatRelayHandler.h"
#include "src/masterd/handlers/mail/MailHandler.h"
#include "src/masterd/mail/MailManager.h"
#include "src/masterd/mail/MysqlMailStore.h"
#include "src/masterd/handlers/quest/QuestHandler.h"
#include "src/masterd/handlers/reputation/ReputationHandler.h"
#include "src/masterd/handlers/arena/ArenaHandler.h"
#include "src/masterd/handlers/battleground/BattleGroundHandler.h"
#include "src/masterd/handlers/outdoorpvp/OutdoorPvpHandler.h"
#include "src/masterd/handlers/weather/WeatherHandler.h"
#include "src/masterd/handlers/events/GameEventHandler.h"
#include "src/masterd/handlers/guild/GuildHandler.h"
#include "src/masterd/handlers/auction/AuctionHandler.h"
#include "src/masterd/handlers/loot/LootHandler.h"
#include "src/masterd/handlers/lunar/LunarHandler.h"
#include "src/masterd/handlers/lfg/LfgHandler.h"
#include "src/masterd/lfg/LfgQueue.h"
#include "src/masterd/handlers/cinematics/CinematicHandler.h"
#include "src/masterd/handlers/skills/SkillHandler.h"
#include "src/masterd/quests/MysqlQuestStateStore.h"
#include "src/masterd/reputation/MysqlReputationStore.h"
#include "src/masterd/reputation/ReputationManager.h"
#include "src/masterd/quests/QuestState.h"
#include "src/masterd/handlers/social/IgnoreListHandler.h"
#include "src/masterd/social/IgnoreList.h"
#include "src/masterd/social/MysqlIgnoreStore.h"
#include "src/masterd/handlers/gmtickets/GmTicketHandler.h"
#include "src/masterd/gmtickets/GmTicketSystem.h"
#include "src/masterd/gmtickets/MysqlGmTicketStore.h"
#include "src/masterd/handlers/trade/TradeHandler.h"
#include "src/masterd/trade/TradeSessionRegistry.h"
#include "src/masterd/session/SessionCharacterMap.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/core/LogConfig.h"

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

	constexpr std::string_view kServerVersion = "0.1.0";

	void PrintStartupBanner()
	{
		std::printf(
			"\n###############################################################\n"
			"#\n"
			"# Serveur\n"
			"# Les Chroniques De La Lune Noire\n"
			"# Version %.*s\n"
			"#\n"
			"###############################################################\n"
			"# Serveur ready\n"
			"###############################################################\n\n",
			static_cast<int>(kServerVersion.size()), kServerVersion.data());
		std::fflush(stdout);
		LOG_INFO(Server, "###############################################################");
		LOG_INFO(Server, "# Serveur — Les Chroniques De La Lune Noire — Version {}", kServerVersion);
		LOG_INFO(Server, "# Serveur ready");
		LOG_INFO(Server, "###############################################################");
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

	// M45 — Construction centralisée des LogSettings (filtres bitmask, fichiers spécialisés
	// GM/Char/DBError/Packet/Custom, couleurs console, seuil fichier distinct).
	engine::core::LogSettings logSettings = engine::core::BuildLogSettingsFromConfig(config, "engine.log");
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
	// Répertoire des templates email HTML (depuis paths.content dans config.json)
	{
		const std::string contentPath = config.GetString("paths.content", "game/data");
		engine::server::SetEmailTemplateDir(contentPath);
		LOG_INFO(Net, "[ServerMain] email template dir: {}", contentPath);
	}
	{
		const bool smtpReady = !smtpConfig.host.empty() && smtpConfig.port != 0;
		const std::string smtpDedicatedLog = config.GetString("log.subsystem_files.Smtp", "");
		if (smtpReady)
		{
			LOG_WARN(Server,
				"[SMTP] Courrier activé : {}:{} (STARTTLS={}, AUTH={}). Traces détaillées : sous-système « Smtp » "
				"(log.level=Info ou Debug) ; fichier dédié : {}",
				smtpConfig.host,
				static_cast<unsigned>(smtpConfig.port),
				smtpConfig.use_starttls ? "oui" : "non",
				smtpConfig.user.empty() ? "non" : "oui",
				smtpDedicatedLog.empty() ? std::string("(désactivé — tout dans le log principal)") : smtpDedicatedLog);
		}
		else
		{
			LOG_WARN(Server,
				"[SMTP] Courrier désactivé (hôte/port absents ou fichier secrets illisible) — pas d’envoi reset / vérification e-mail / CGU.");
		}
	}
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

	engine::server::CharacterListHandler characterListHandler;
	characterListHandler.SetServer(&server);
	characterListHandler.SetSessionManager(&sessionManager);
	characterListHandler.SetConnectionSessionMap(&connSessionMap);
	characterListHandler.SetConnectionPool(&dbPool);

	engine::server::CharacterDeleteHandler characterDeleteHandler;
	characterDeleteHandler.SetServer(&server);
	characterDeleteHandler.SetSessionManager(&sessionManager);
	characterDeleteHandler.SetConnectionSessionMap(&connSessionMap);
	characterDeleteHandler.SetConnectionPool(&dbPool);

	engine::server::CharacterSavePositionHandler characterSavePositionHandler;
	characterSavePositionHandler.SetServer(&server);
	characterSavePositionHandler.SetSessionManager(&sessionManager);
	characterSavePositionHandler.SetConnectionSessionMap(&connSessionMap);
	characterSavePositionHandler.SetConnectionPool(&dbPool);

	// Phase 4 chat — mapping connId → (character_id, character_name) pour sender display + whisper target.
	engine::server::SessionCharacterMap sessionCharMap;

	engine::server::CharacterEnterWorldHandler characterEnterWorldHandler;
	characterEnterWorldHandler.SetServer(&server);
	characterEnterWorldHandler.SetSessionManager(&sessionManager);
	characterEnterWorldHandler.SetConnectionSessionMap(&connSessionMap);
	characterEnterWorldHandler.SetSessionCharacterMap(&sessionCharMap);
	characterEnterWorldHandler.SetConnectionPool(&dbPool);

	// Chat MVP — handler de relais des messages chat (broadcast à toutes les sessions actives).
	engine::server::ChatRelayHandler chatRelayHandler;
	chatRelayHandler.SetServer(&server);
	chatRelayHandler.SetSessionManager(&sessionManager);
	chatRelayHandler.SetConnectionSessionMap(&connSessionMap);
	chatRelayHandler.SetSessionCharacterMap(&sessionCharMap);
	chatRelayHandler.SetAccountStore(accountStore);
	// Phase 5.1 — pool DB pour le routage guild (SQL guild_members).
	chatRelayHandler.SetConnectionPool(&dbPool);
	// Phase 2 CMANGOS.01 — sanitizer + gate (chat hardening).
	{
		engine::server::chat::ChatSanitizerConfig sanCfg;
		sanCfg.maxMessageBytes = static_cast<size_t>(std::max<int64_t>(
			16, config.GetInt("chat.sanitizer.max_message_bytes", 255)));
		sanCfg.stripZeroWidth = config.GetBool("chat.sanitizer.strip_zero_width", true);
		chatRelayHandler.SetSanitizerConfig(sanCfg);

		engine::server::chat::ChatGateConfig gateCfg;
		gateCfg.floodWindowMs = static_cast<uint64_t>(std::max<int64_t>(
			0, config.GetInt("chat.gate.flood_window_ms", 5000)));
		gateCfg.floodMaxMessages = static_cast<size_t>(std::max<int64_t>(
			1, config.GetInt("chat.gate.flood_max_messages", 5)));
		gateCfg.maxTrackedAccounts = static_cast<size_t>(std::max<int64_t>(
			0, config.GetInt("chat.gate.max_tracked_accounts", 4096)));
		chatRelayHandler.Gate().Reconfigure(gateCfg);
		chatRelayHandler.Gate().WireProduction(&dbPool, accountStore);
		LOG_INFO(Net, "[ServerMain] ChatGate configured (window={}ms max={}/win) (CMANGOS.01)",
			gateCfg.floodWindowMs, gateCfg.floodMaxMessages);
	}

	// CMANGOS.18 (Phase 3.18 step 3) — Mail wire server.
	// Le store DB est instancié seulement si dbPool est dispo (sinon le master
	// tourne en mode "no-DB" — accountStoreMem — et on ne peut pas persister
	// de mails). Dans ce mode dégradé, on laisse m_mgr=nullptr côté handler →
	// HandlePacket logge un warning et drop. C'est cohérent avec le fait qu'en
	// mode no-DB, l'auth n'est pas non plus persistée et la mailbox n'a pas
	// de raison d'être réellement utilisable.
	engine::server::mail::MysqlMailStore mailStore(&dbPool);
	engine::server::mail::MailManager mailManager(&mailStore);
	engine::server::MailHandler mailHandler;
	if (dbPool.IsInitialized())
	{
		mailHandler.SetMailManager(&mailManager);
		mailHandler.SetServer(&server);
		mailHandler.SetSessionManager(&sessionManager);
		mailHandler.SetConnectionSessionMap(&connSessionMap);
		LOG_INFO(Net, "[ServerMain] MailHandler configured (CMANGOS.18 step 3)");
	}
	else
	{
		LOG_WARN(Net, "[ServerMain] MailHandler skipped (DB pool unavailable)");
	}

	// CMANGOS.23 (Phase 5.23 step 3+4) — Quest wire server.
	// Le tracker in-memory est l'autorite runtime des etats Quest. Le store
	// MySQL sert de persistance audit (et future reload au login). En mode
	// no-DB on garde le tracker mais on saute le store : la perte au reboot
	// est acceptable pour cette MVP (le client redemandera la liste).
	engine::server::quests::QuestStateTracker questTracker;
	engine::server::quests::MysqlQuestStateStore questStore(&dbPool);
	engine::server::QuestHandler questHandler;
	questHandler.SetTracker(&questTracker);
	questHandler.SetServer(&server);
	questHandler.SetSessionManager(&sessionManager);
	questHandler.SetConnectionSessionMap(&connSessionMap);
	if (dbPool.IsInitialized())
	{
		questHandler.SetStore(&questStore);
		LOG_INFO(Net, "[ServerMain] QuestHandler configured with DB store (CMANGOS.23 step 3+4)");
	}
	else
	{
		LOG_WARN(Net, "[ServerMain] QuestHandler running in no-DB mode (no persistence)");
	}

	// CMANGOS.25 (Phase 3.25 step 3+4) — IgnoreList wire server.
	// Le store DB est instancie seulement si dbPool est dispo (sinon mode no-DB :
	// le manager prend un store nul -> opErr toujours NotIgnored, ce qui est
	// coherent avec le ticket : feature desactivee silencieusement en mode degrade).
	// Le manager est aussi cable sur ChatRelayHandler pour le filter whisper/chat.
	engine::server::social::MysqlIgnoreStore ignoreStore(&dbPool);
	engine::server::social::IgnoreListManager ignoreManager(&ignoreStore);
	engine::server::IgnoreListHandler ignoreListHandler;
	if (dbPool.IsInitialized())
	{
		ignoreListHandler.SetManager(&ignoreManager);
		ignoreListHandler.SetServer(&server);
		ignoreListHandler.SetSessionManager(&sessionManager);
		ignoreListHandler.SetConnectionSessionMap(&connSessionMap);
		// Cable le manager sur ChatRelayHandler pour le filter ignore lors des whispers.
		chatRelayHandler.SetIgnoreManager(&ignoreManager);
		LOG_INFO(Net, "[ServerMain] IgnoreListHandler configured + ChatRelayHandler ignore filter armed (CMANGOS.25 step 3+4)");
	}
	else
	{
		LOG_WARN(Net, "[ServerMain] IgnoreListHandler skipped (DB pool unavailable)");
	}

	// CMANGOS.32 (Phase 5.32 step 3+4) — GmTickets wire server.
	// Le system in-memory est l'autorite runtime des tickets ouverts. Le store
	// MySQL sert de persistance audit (et future reload au login). En mode
	// no-DB on garde le system mais on saute le store : la perte au reboot
	// est acceptable pour cette MVP (le client redemandera la liste).
	engine::server::gmtickets::GmTicketSystem gmTicketSystem;
	engine::server::gmtickets::MysqlGmTicketStore gmTicketStore(&dbPool);
	engine::server::GmTicketHandler gmTicketHandler;
	gmTicketHandler.SetSystem(&gmTicketSystem);
	gmTicketHandler.SetServer(&server);
	gmTicketHandler.SetSessionManager(&sessionManager);
	gmTicketHandler.SetConnectionSessionMap(&connSessionMap);
	if (dbPool.IsInitialized())
	{
		gmTicketHandler.SetStore(&gmTicketStore);
		LOG_INFO(Net, "[ServerMain] GmTicketHandler configured with DB store (CMANGOS.32 step 3+4)");
	}
	else
	{
		LOG_WARN(Net, "[ServerMain] GmTicketHandler running in no-DB mode (no persistence)");
	}

	// CMANGOS.27 (Phase 4.27 step 3+4) -- Trade wire server. Le registry est
	// transient (pas persiste cote DB) : au reboot, toutes les trades en cours
	// sont implicitement perdues, ce qui est acceptable pour le V1 (les
	// clients re-affichent une UI vide au prochain login). Les opcodes 83/86/
	// 88/91/93 sont dispatches au handler ; les responses 84/87/89/92 et les
	// push notifications 85/90/94 sont emis par le handler aux participants.
	engine::server::trade::TradeSessionRegistry tradeRegistry;
	engine::server::TradeHandler tradeHandler;
	tradeHandler.SetRegistry(&tradeRegistry);
	tradeHandler.SetServer(&server);
	tradeHandler.SetSessionManager(&sessionManager);
	tradeHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] TradeHandler configured (CMANGOS.27 step 3+4, transient registry)");

	// CMANGOS.24 (Phase 3.24 step 3+4) — Reputation wire server.
	// Le manager in-memory est l'autorite runtime des reputations + spillover.
	// Le store MySQL sert de persistance (et de source de verite pour la
	// REPUTATION_LIST_REQUEST en V1, cf. ReputationHandler::HandleListRequest).
	// En mode no-DB, la liste retournee au client est vide (le manager n'expose
	// pas d'iteration directe sur les factions d'un account en V1).
	engine::server::reputation::ReputationManager reputationManager;
	engine::server::reputation::MysqlReputationStore reputationStore(&dbPool);
	engine::server::ReputationHandler reputationHandler;
	reputationHandler.SetManager(&reputationManager);
	reputationHandler.SetServer(&server);
	reputationHandler.SetSessionManager(&sessionManager);
	reputationHandler.SetConnectionSessionMap(&connSessionMap);
	if (dbPool.IsInitialized())
	{
		reputationHandler.SetStore(&reputationStore);
		LOG_INFO(Net, "[ServerMain] ReputationHandler configured with DB store (CMANGOS.24 step 3+4)");
	}
	else
	{
		LOG_WARN(Net, "[ServerMain] ReputationHandler running in no-DB mode (List will return empty)");
	}

	// CMANGOS.33 (Phase 5.33 step 3+4) — LookForGroup wire server.
	// La queue est transient (pas de persistance DB) : au reboot, toutes
	// les inscriptions LFG sont perdues, ce qui est acceptable pour le V1
	// (les clients re-affichent une UI vide au prochain login). Les opcodes
	// 100/102/104/107 sont dispatches au handler ; les responses 101/103/105
	// et la push notification 106 sont emis par le handler aux participants.
	// Note V1 : TickMatchmaking n'est pas appele en boucle automatique ; un
	// timer 5s sera cable dans une sub-PR future.
	engine::server::lfg::LfgQueue lfgQueue;
	engine::server::LfgHandler lfgHandler;
	lfgHandler.SetQueue(&lfgQueue);
	lfgHandler.SetServer(&server);
	lfgHandler.SetSessionManager(&sessionManager);
	lfgHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] LfgHandler configured (CMANGOS.33 step 3+4, transient queue)");

	// CMANGOS.30 (Phase 5.30 step 3+4) — CinematicHandler. Le master pousse une
	// cinematic au client (intro, fin de quete, etc.) via PushCinematic. Les
	// opcodes 109 (Ack) et 111 (Skip) sont dispatches au handler ; les
	// responses 110/112 sont emises par le handler. La push notification 108
	// est emise par d'autres handlers (Quest reward, etc.) via
	// CinematicHandler::PushCinematic. V1 : pas de tracking server-side.
	engine::server::CinematicHandler cinematicHandler;
	cinematicHandler.SetServer(&server);
	cinematicHandler.SetSessionManager(&sessionManager);
	cinematicHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] CinematicHandler configured (CMANGOS.30 step 3+4, push-only)");

	// CMANGOS.39 (Phase 4.39 step 3+4) — Skills wire server.
	// Le master tient en memoire la skill book par account (V1, starter set
	// hardcode : Cooking, Herbalism, Mining, FirstAid, Lockpicking). Les
	// opcodes 113/115/117 sont dispatches au handler ; les responses
	// 114/116/118 et la push notification 119 sont emis par le handler aux
	// clients. Pas de persistance DB en V1 (sub-PR future avec migration
	// MysqlSkillStore). PushSkillUpgrade est expose pour usage futur par
	// le CraftingSystem ou autres handlers.
	engine::server::SkillHandler skillHandler;
	skillHandler.SetServer(&server);
	skillHandler.SetSessionManager(&sessionManager);
	skillHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] SkillHandler configured (CMANGOS.39 step 3+4, in-memory store)");

	// CMANGOS.21 (Phase 5.21 step 3+4) — Arena wire server.
	// Le master tient en memoire un ArenaTeamRegistry (V1, seed hardcode
	// par account au premier acces : 3 teams 2v2/3v3/5v5 a rating 1500)
	// + une queue par account + un map de proposals actifs. Les opcodes
	// 120/122/124/127 sont dispatches au handler ; les responses
	// 121/123/125/128 et les push notifications 126 (MatchProposal) +
	// 129 (MatchResult) sont emises par le handler.
	// V1 limitations : match contre AI Team Alpha fictif, result win/loss
	// random 50%, pas de SyncArena RPC entre master et shardd. Pas de
	// persistance DB (sub-PR future avec MysqlArenaStore).
	engine::server::ArenaHandler arenaHandler;
	arenaHandler.SetServer(&server);
	arenaHandler.SetSessionManager(&sessionManager);
	arenaHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] ArenaHandler configured (CMANGOS.21 step 3+4, in-memory registry)");

	// CMANGOS.10 (Phase 5 step 3+4) — BattleGround wire server.
	// Le master tient en memoire un store de queue + matches actifs
	// (V1 : 3 BG hardcodes Warsong/Arathi/Alterac, queue par account,
	// match V1 vs AI bot fictif a la queue). Les opcodes 130/132/134/139
	// sont dispatches au handler ; les responses 131/133/135 et les push
	// notifications 136 (MatchStart) / 137 (ScoreUpdate) / 138 (MatchEnd)
	// sont emises par le handler.
	// V1 limitations : match vs AI bot, score evolution simulee
	// instantanee, winnerFaction tirage 50/50, pas de SyncBg RPC entre
	// master et shardd. Pas de persistance DB.
	engine::server::BattleGroundHandler bgHandler;
	bgHandler.SetServer(&server);
	bgHandler.SetSessionManager(&sessionManager);
	bgHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] BattleGroundHandler configured (CMANGOS.10 step 3+4, in-memory)");

	// CMANGOS.36 (Phase 5.36 step 3+4) — OutdoorPvp wire server.
	// Le master tient en memoire un OutdoorPvPManager seede au boot avec
	// 2 zones contestees (Hellfire Peninsula 3 obj, Eastern Plaguelands
	// 4 obj). Les opcodes 140/142/144/146 sont dispatches au handler ;
	// les responses 141/143/145/147 et les push notifications 148
	// (CaptureProgress) / 149 (CaptureCompleted) sont emises par le
	// handler. V1 limitations : capture simulee instantanement, pas de
	// SyncOutdoorPvp RPC entre master et shardd. Pas de persistance DB.
	engine::server::OutdoorPvpHandler outdoorPvpHandler;
	outdoorPvpHandler.SetServer(&server);
	outdoorPvpHandler.SetSessionManager(&sessionManager);
	outdoorPvpHandler.SetConnectionSessionMap(&connSessionMap);
	outdoorPvpHandler.SeedV1Zones();
	LOG_INFO(Net, "[ServerMain] OutdoorPvpHandler configured (CMANGOS.36 step 3+4, in-memory, 2 zones seed)");

	// CMANGOS.42 (Phase 4.42 step 3+4) — Weather wire server.
	// Le master tient en memoire un WeatherManager seede au boot avec 3 zones
	// meteo (Stormwind Plains tempere, Frozen Tundra polaire, Tanaris Desert
	// aride). Les opcodes 150/152/154 sont dispatches au handler ; les
	// responses 151/153/155 et la push notification 156 (WeatherUpdate)
	// sont emises par le handler. V1 limitations : tick simule a chaque
	// SubscribeRequest (force reroll), pas de SyncWeather RPC entre master
	// et shardd. Pas de persistance DB.
	engine::server::WeatherHandler weatherHandler;
	weatherHandler.SetServer(&server);
	weatherHandler.SetSessionManager(&sessionManager);
	weatherHandler.SetConnectionSessionMap(&connSessionMap);
	weatherHandler.SeedV1Zones();
	LOG_INFO(Net, "[ServerMain] WeatherHandler configured (CMANGOS.42 step 3+4, in-memory, 3 zones seed)");

	// CMANGOS.31 (Phase 5.31 step 3+4) — GameEventHandler : list/subscribe
	// global + push StateChange. V1 : 4 events seedees au boot (Halloween,
	// Winter Veil, Lunar Festival, Midsummer Fire). Les opcodes 157/159/161
	// sont dispatches au handler ; les responses 158/160/162 et la push
	// notification 163 (StateChange) sont emises par le handler. V1
	// limitations : subscribe = snapshot one-shot (pas de broadcast
	// cross-subscribers, pas de tick periodique). Pas de SyncGameEvents RPC
	// entre master et shardd.
	engine::server::GameEventHandler gameEventHandler;
	gameEventHandler.SetServer(&server);
	gameEventHandler.SetSessionManager(&sessionManager);
	gameEventHandler.SetConnectionSessionMap(&connSessionMap);
	gameEventHandler.SeedV1Events();
	LOG_INFO(Net, "[ServerMain] GameEventHandler configured (CMANGOS.31 step 3+4, in-memory, 4 events seed)");

	// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — GuildHandler : list /
	// members / permissions / bank + push MotdUpdate. V1 : 2 guildes
	// seedees au boot ("Les Gardiens", "L'Ombre") avec membres + bank
	// + WoW perms defaults par rang. Les opcodes 164/166/168/170 sont
	// dispatches au handler ; les responses 165/167/169/171 et la push
	// notification 172 (MotdUpdate) sont emises par le handler. V1
	// limitations : pas de filtrage par account membership, bank tab 0
	// only, lecture seule (pas de modification client). Pas de SyncGuilds
	// RPC entre master et shardd.
	engine::server::GuildHandler guildHandler;
	guildHandler.SetServer(&server);
	guildHandler.SetSessionManager(&sessionManager);
	guildHandler.SetConnectionSessionMap(&connSessionMap);
	guildHandler.SeedV1Guilds();
	LOG_INFO(Net, "[ServerMain] GuildHandler configured (CMANGOS.21 step 3+4 Guilds, in-memory, 2 guilds seed)");

	// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — AuctionHandler : list /
	// post / bid / cancel + push AuctionExpired. V1 : 8 listings hardcodes
	// au boot avec differents owners (Aragorn, Legolas, Gimli, Saruman) et
	// expirations echelonnees (1h-48h). Les opcodes 173/175/177/179 sont
	// dispatches au handler ; les responses 174/176/178/180 et la push
	// notification 181 (AuctionExpired) sont emises par le handler. V1
	// limitations : item name table 10 entries hardcode, owner name =
	// "Account#<id>", pas de paiement reel (economie cosmetique), pas de
	// SyncAuction RPC entre master et shardd.
	engine::server::AuctionHandler auctionHandler;
	auctionHandler.SetServer(&server);
	auctionHandler.SetSessionManager(&sessionManager);
	auctionHandler.SetConnectionSessionMap(&connSessionMap);
	auctionHandler.SeedV1Auctions();
	LOG_INFO(Net, "[ServerMain] AuctionHandler configured (CMANGOS.09 step 3+4 AuctionHouse, in-memory, 8 listings seed)");

	// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — LootHandler : Choice
	// (Need/Greed/Pass) + SimulateRoll DEBUG + push RollNotification + push
	// RollResultNotification. V1 simulation simple : un seul eligible par roll
	// (le creator), items hardcodes (5 entries), random roll 0..100, regle
	// Need > Greed > Pass + plus haut roll dans la meme categorie. Les
	// opcodes 183/186 sont dispatches au handler ; les responses 184/187 et
	// les push notifications 182/185 sont emises par le handler. V1
	// limitations : pas de groupe (CMANGOS.15), items hardcodes, pas de
	// timeout tick periodique (scan a chaque HandleChoice), pas de SyncLoot
	// RPC entre master et shardd.
	engine::server::LootHandler lootHandler;
	lootHandler.SetServer(&server);
	lootHandler.SetSessionManager(&sessionManager);
	lootHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] LootHandler configured (CMANGOS.17 step 3+4 Loot, in-memory, simulation V1)");

	// Phase 5 step 3+4 Lunar — LunarHandler : etat lunaire authoritative
	// (16 phases, cycle 14 jours reels, deterministe depuis epoch). Tick
	// periodique (5 min) detecte changement de phase et push broadcast.
	// Pas de Subscribe : la lune est globale.
	engine::server::LunarHandler lunarHandler;
	lunarHandler.SetServer(&server);
	lunarHandler.SetSessionManager(&sessionManager);
	lunarHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] LunarHandler configured (Phase 5 Lunar, cycle 14j, 16 phases)");

	// Premier Tick au boot : etablit la phase courante (m_lastBroadcastPhase
	// passe de 0xFF a la valeur reelle, broadcast initial aux clients
	// connectes — V1 il n'y en a pas encore puisque le serveur vient de
	// demarrer, mais c'est defensif et coherent).
	{
		const uint64_t bootNowMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		lunarHandler.Tick(bootNowMs);
	}

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

	engine::server::ServerRegistry serverRegistry;
	// Le master ne s'auto-enregistre dans game_servers que si server.self_register=true.
	// Défaut false : en déploiement master/shard séparé, l'entrée master pollue la liste
	// des shards renvoyée par SERVER_LIST (port 3840 = auth, pas gameplay).
	const bool selfRegister = config.GetBool("server.self_register", false);
	if (selfRegister)
	{
		serverRegistry.RegisterSelf(config);
		serverListHandler.SetServerRegistry(&serverRegistry);
	}
	else
	{
		LOG_INFO(Server, "[MAIN_SRV] server.self_register=false : master non listé dans game_servers (SERVER_LIST ne renvoie que les shards)");
	}
	PrintStartupBanner();

	LOG_DEBUG(Server, "[MAIN_SRV] avant SetPacketHandler");
	server.SetPacketHandler([&authHandler, &shardRegisterHandler, &shardTicketHandler, &serverListHandler, &passwordResetHandler, &termsHandler, &characterCreateHandler, &characterListHandler, &characterDeleteHandler, &characterSavePositionHandler, &chatRelayHandler, &characterEnterWorldHandler, &mailHandler, &questHandler, &ignoreListHandler, &gmTicketHandler, &tradeHandler, &reputationHandler, &lfgHandler, &cinematicHandler, &skillHandler, &arenaHandler, &bgHandler, &outdoorPvpHandler, &weatherHandler, &gameEventHandler, &guildHandler, &auctionHandler, &lootHandler, &lunarHandler](uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
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
		else if (opcode == kOpcodeCharacterListRequest)
			characterListHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeCharacterDeleteRequest)
			characterDeleteHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeCharacterSavePositionRequest)
			characterSavePositionHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeChatSendRequest)
			chatRelayHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeCharacterEnterWorldRequest)
			characterEnterWorldHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeMailSendRequest
		      || opcode == kOpcodeMailListInboxRequest
		      || opcode == kOpcodeMailReadRequest
		      || opcode == kOpcodeMailTakeAttachmentsRequest
		      || opcode == kOpcodeMailDeleteRequest)
			mailHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeQuestAcceptRequest
		      || opcode == kOpcodeQuestCompleteRequest
		      || opcode == kOpcodeQuestRewardRequest
		      || opcode == kOpcodeQuestListRequest)
			questHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeIgnoreAddRequest
		      || opcode == kOpcodeIgnoreRemoveRequest
		      || opcode == kOpcodeIgnoreListRequest)
			ignoreListHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeGmTicketOpenRequest
		      || opcode == kOpcodeGmTicketListMineRequest
		      || opcode == kOpcodeGmTicketCancelRequest)
			gmTicketHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeTradeBeginRequest
		      || opcode == kOpcodeTradeSetOfferRequest
		      || opcode == kOpcodeTradeLockRequest
		      || opcode == kOpcodeTradeCommitRequest
		      || opcode == kOpcodeTradeCancelRequest)
			tradeHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeReputationListRequest)
			reputationHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeLfgQueueRequest
		      || opcode == kOpcodeLfgLeaveRequest
		      || opcode == kOpcodeLfgStatusRequest
		      || opcode == kOpcodeLfgMatchAcceptRequest)
			lfgHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeCinematicAckRequest
		      || opcode == kOpcodeCinematicSkipRequest)
			cinematicHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeSkillsListRequest
		      || opcode == kOpcodeSkillLearnRequest
		      || opcode == kOpcodeSkillUseRequest)
			skillHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeArenaTeamListRequest
		      || opcode == kOpcodeArenaQueueRequest
		      || opcode == kOpcodeArenaLeaveQueueRequest
		      || opcode == kOpcodeArenaMatchAcceptRequest)
			arenaHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeBgListRequest
		      || opcode == kOpcodeBgQueueRequest
		      || opcode == kOpcodeBgLeaveQueueRequest
		      || opcode == kOpcodeBgLeaveMatchRequest)
			bgHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeOutdoorPvpZoneListRequest
		      || opcode == kOpcodeOutdoorPvpSubscribeRequest
		      || opcode == kOpcodeOutdoorPvpUnsubscribeRequest
		      || opcode == kOpcodeOutdoorPvpCaptureStartRequest)
			outdoorPvpHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeWeatherListRequest
		      || opcode == kOpcodeWeatherSubscribeRequest
		      || opcode == kOpcodeWeatherUnsubscribeRequest)
			weatherHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeGameEventListRequest
		      || opcode == kOpcodeGameEventSubscribeRequest
		      || opcode == kOpcodeGameEventUnsubscribeRequest)
			gameEventHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeGuildListRequest
		      || opcode == kOpcodeGuildMembersRequest
		      || opcode == kOpcodeGuildPermissionsRequest
		      || opcode == kOpcodeGuildBankRequest)
			guildHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeAuctionListRequest
		      || opcode == kOpcodeAuctionPostRequest
		      || opcode == kOpcodeAuctionBidRequest
		      || opcode == kOpcodeAuctionCancelRequest)
			auctionHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeLootRollChoiceRequest
		      || opcode == kOpcodeLootSimulateRollRequest)
			lootHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
		else if (opcode == kOpcodeLunarStateRequest)
			lunarHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
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

		// Source de verite pour le compteur joueurs : SessionCharacterMap (cote master)
		// qui contient les connId post-EnterWorld. Avant : on utilisait le current_load
		// du heartbeat shard, mais le shard binaire ne voyait que les connexions TCP
		// transitoires (handshake ticket -> close), ce qui donnait toujours 0.
		// Pour le dev avec un seul shard, on assigne tout le compte EnterWorld au shard
		// online unique. A generaliser quand multi-shard arrivera (mapping char->shard).
		const uint64_t playersFromMaster = static_cast<uint64_t>(sessionCharMap.Count());
		uint32_t shardsOnline = 0;
		for (const auto& s : shards)
		{
			const bool ok = (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded);
			if (ok)
				++shardsOnline;
		}
		const uint64_t totalPlayers = playersFromMaster;

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
			// Si un seul shard online, on lui assigne 100% des EnterWorld actifs.
			// Sinon, on retombe sur s.current_load (heartbeat) -- pas exact mais
			// au moins distribue.
			const bool singleShardOnline = (shardsOnline == 1u);
			for (const auto& s : shards)
			{
				const bool ok = (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded);
				const uint32_t players = (ok && singleShardOnline)
					? static_cast<uint32_t>(playersFromMaster)
					: s.current_load;
				appendServer(s.name, ok, players, s.endpoint, s.region, s.max_capacity, s.state);
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

		// Cf. statusProvider plus haut : SessionCharacterMap.Count() est la source
		// de verite (post-EnterWorld) plutot que s.current_load (heartbeat shard).
		const uint64_t totalPlayers = static_cast<uint64_t>(sessionCharMap.Count());
		uint32_t onlineServers = 0;
		for (const auto& s : shards)
		{
			const bool ok = (s.state == engine::server::ShardState::Online || s.state == engine::server::ShardState::Degraded);
			if (ok)
				++onlineServers;
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

	// Phase 5 Lunar — Tick periodique (5 min) pour detecter changement de phase
	// et push broadcast aux clients connectes. Le calcul est deterministe via
	// LunarCalendar : la phase courante est purement fonction du timestamp
	// realNowMs + cycleStart + cycleDuration. Le Tick compare avec la derniere
	// phase broadcastee (m_lastBroadcastPhase) et n'emet un push que si
	// different.
	auto lastLunarTickTime = std::chrono::steady_clock::now();
	constexpr auto kLunarTickInterval = std::chrono::seconds(300);

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
				// Phase 4 chat — purge le binding character actif pour ne pas leak
				// dans le whisper directory.
				sessionCharMap.Remove(connId);
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

		// Phase 5 Lunar — Tick periodique (5 min) detection changement de phase.
		// Le Tick compare avec la derniere phase broadcastee et push 194 si different.
		if (now - lastLunarTickTime >= kLunarTickInterval)
		{
			const uint64_t realNowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			lunarHandler.Tick(realNowMs);
			lastLunarTickTime = now;
		}
	}

	serverRegistry.SetOffline();
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
