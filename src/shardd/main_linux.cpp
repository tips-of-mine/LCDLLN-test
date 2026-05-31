/// M22.6 — Shard server entry point: accepts client connections; first packet must be PRESENT_SHARD_TICKET; validates and responds TICKET_ACCEPTED/REJECTED.

#include "src/masterd/metrics/HealthEndpoint.h"
#include "src/masterd/metrics/PrometheusMetrics.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/PacketLog.h"
#include "src/masterd/handlers/shard/ShardTicketValidator.h"
#include "src/masterd/handlers/shard/ShardTicketHandshakeHandler.h"
#include "src/shardd/world/AdmittedCharacterRegistry.h"
#include "src/shared/server_bootstrap/ServerApp.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ShardToMasterClient.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/core/LogConfig.h"
// Wave 6 — Wiring runtime des modules internes (EventAI + PoolManager).
#include "src/shardd/ai/EventAIRuntime.h"
#include "src/shardd/pools/PoolManagerRuntime.h"
// Wave 8 — Wiring runtime des modules internes (ThreatList + DBScripts).
#include "src/shardd/combat/ThreatListRuntime.h"
#include "src/shardd/dbscripts/DBScriptRuntime.h"
// Wave 9 — Wiring runtime AntiCheat + SpellFamily + InstanceManager.
#include "src/shardd/anticheat/AntiCheatGameplayRuntime.h"
#include "src/shardd/spell/SpellFamilyRuntime.h"
#include "src/shardd/maps/InstanceManagerRuntime.h"

#include <csignal>
#include <chrono>
#include <algorithm>
#include <memory>
#include <thread>

namespace
{
	volatile sig_atomic_t g_quit = 0;
	void OnSignal(int) { g_quit = 1; }
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	// M45 — Construction centralisée des LogSettings (filtres, fichiers spécialisés, couleurs).
	engine::core::LogSettings logSettings = engine::core::BuildLogSettingsFromConfig(config, "shard.log");
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

	int64_t gamePort = config.GetInt("server.tcp.port", 0);
	if (gamePort <= 0)
		gamePort = config.GetInt("shard.port", 3843);
	uint16_t port = static_cast<uint16_t>(gamePort);
	if (!server.Init(port, netConfig))
	{
		LOG_ERROR(Net, "[ShardMain] NetServer Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}
	LOG_INFO(Net, "[ShardMain] NetServer listening on port {}", port);

	// Wave 14 — PacketLog opt-in (debug RX/TX trace). Desactive par defaut.
	// Memes cles de config que masterd : `server.debug.packetlog.enabled`
	// + `server.debug.packetlog.capacity` (defaut 512). Le pointeur reste
	// vivant tant que le shard tourne (detruit en sortie de main, apres
	// server.Shutdown() implicite).
	std::unique_ptr<engine::server::netdebug::PacketLog> packetLogOpt;
	const bool packetLogEnabled = config.GetBool("server.debug.packetlog.enabled", false);
	if (packetLogEnabled)
	{
		const int capacityCfg = static_cast<int>(config.GetInt("server.debug.packetlog.capacity", 512));
		const size_t capacity = capacityCfg > 0 ? static_cast<size_t>(capacityCfg) : 512u;
		packetLogOpt = std::make_unique<engine::server::netdebug::PacketLog>(capacity);
		server.SetPacketLog(packetLogOpt.get());
		LOG_INFO(Net, "[PacketLog] enabled (capacity={})", capacity);
	}

	// M23.1 + M23.2 — Health/readiness and Prometheus /metrics (Shard: no auth/sessions/shard registry/DB).
	engine::server::HealthEndpoint healthEndpoint;
	uint16_t healthPort = static_cast<uint16_t>(config.GetInt("server.health.port", 3844));
	std::string healthBind = config.GetString("server.health.bind", "127.0.0.1");
	auto metricsProvider = [&server]() {
		engine::server::NetServerStats netStats;
		server.GetNetworkStats(netStats);
		return engine::server::BuildPrometheusText(netStats, 0, 0, 0, 0, nullptr);
	};
	if (healthEndpoint.Init(healthPort, healthBind, []() { return true; }, metricsProvider, nullptr, nullptr))
		LOG_INFO(Net, "[ShardMain] Health endpoint listening on {}:{} (/healthz, /readyz, /metrics)", healthBind, healthPort);
	else
		LOG_WARN(Net, "[ShardMain] Health endpoint Init failed (port {}), continuing without health endpoint", healthPort);

	engine::server::ShardTicketValidator validator;
	validator.SetSecret(config.GetString("shard.ticket_hmac_secret", ""));
	validator.SetShardId(static_cast<uint32_t>(config.GetInt("shard.id", 1)));

	engine::server::ShardTicketHandshakeHandler handshakeHandler;
	handshakeHandler.SetServer(&server);
	handshakeHandler.SetValidator(&validator);
	// TA.3 : registre d'admission partagé — le handshake TCP y inscrit le character_id
	// authentifié du ticket ; le gate UDP (ServerApp::HandleHello) le consulte.
	engine::server::AdmittedCharacterRegistry admittedRegistry;
	handshakeHandler.SetAdmittedCharacterRegistry(&admittedRegistry);
	server.SetPacketHandler([&handshakeHandler](uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionId,
		const uint8_t* payload, size_t payloadSize) {
		handshakeHandler.HandlePacket(connId, opcode, requestId, sessionId, payload, payloadSize);
	});

	const std::string masterHost = config.GetString("shard.master.host", config.GetString("shard.master_host", "127.0.0.1"));
	const uint16_t masterPort = static_cast<uint16_t>(config.GetInt("shard.master.port", config.GetInt("shard.master_port", 3840)));
	const std::string regName = config.GetString("shard.register.name", config.GetString("shard.register_name", "shard"));
	const std::string regEndpoint = config.GetString("shard.register.endpoint", config.GetString("shard.register_endpoint", "127.0.0.1:3843"));
	// TB.1 : endpoint UDP gameplay annoncé au master (relayé au client via SERVER_LIST). Vide = non annoncé.
	const std::string regUdpEndpoint = config.GetString("shard.register.udp_endpoint", "");
	const uint32_t regCap = static_cast<uint32_t>(config.GetInt("shard.register.max_capacity", config.GetInt("shard.register_max_capacity", 500)));
	const std::string buildVer = config.GetString("shard.register.build_version", config.GetString("shard.build_version", "dev"));
	// Présentation publique du serveur dans la liste client (M-server-meta).
	// display_name : repli sur le nom technique si absent.
	// game_mode : "pve" / "pvp" (défaut pve). ruleset : "cooperative" / "competitive"
	// / "hardcore" / "roleplay" (défaut cooperative).
	const std::string regDisplayName = config.GetString("shard.register.display_name", regName);
	const engine::network::ShardGameMode regGameMode =
		engine::network::ParseGameMode(config.GetString("shard.register.game_mode", "pve"));
	const engine::network::ShardRuleset regRuleset =
		engine::network::ParseRuleset(config.GetString("shard.register.ruleset", "cooperative"));
	// Région annoncée (texte libre, ex. « eu-west »), remontée telle quelle dans l'API /status.
	const std::string regRegion = config.GetString("shard.register.region", config.GetString("shard.register_region", ""));
	const std::string masterTlsFp = config.GetString("shard.master.tls_fingerprint", config.GetString("shard.master_tls_fingerprint", ""));
	const bool masterInsecure = config.GetBool("shard.master.allow_insecure_dev", config.GetBool("shard.master_allow_insecure_dev", false));

	engine::network::ShardToMasterClient toMaster;
	toMaster.SetMasterAddress(masterHost, masterPort);
	if (!masterTlsFp.empty())
		toMaster.SetExpectedServerFingerprint(masterTlsFp);
	toMaster.SetAllowInsecureDev(masterInsecure);
	toMaster.SetShardIdentity(regName, regEndpoint, regUdpEndpoint, regCap, buildVer, regDisplayName, regGameMode, regRuleset, regRegion);
	toMaster.SetHeartbeatIntervalSec(static_cast<int>(config.GetInt("shard.heartbeat_interval_sec", 10)));
	// TA.3 — admet (account_id, character_id) dans le registre dès que le master pousse
	// kOpcodeMasterToShardAdmitCharacter (suite à un EnterWorld réussi côté master). Sans
	// ce câblage, le Hello UDP du client (clientNonce=character_id) serait rejeté car le
	// ticket TCP avait été émis avant le choix de perso (character_id=0 → non admis).
	toMaster.SetAdmitCharacterCallback([&admittedRegistry](uint64_t account_id, uint64_t character_id,
		std::string_view character_name, std::string_view gender) {
		const std::uint64_t nowMs = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		admittedRegistry.Admit(character_id, account_id, character_name, gender, nowMs);
		LOG_INFO(Net, "[ShardMain] Admit pushed by master (account_id={}, character_id={}, name='{}', gender='{}', nowMs={})",
			account_id, character_id, character_name, gender, nowMs);
	});
	toMaster.Start();
	// TA.3 : boucle gameplay UDP (ServerApp) sur un thread dedie, gated par admittedRegistry.
	// Cohabite avec la stack TCP ticket + heartbeat + runtimes (ports/protocoles distincts).
	engine::server::ServerApp gameplayApp(config);
	gameplayApp.SetAdmittedCharacterRegistry(&admittedRegistry);
	// Présence enrichie (web-portal) : le heartbeat shard→master joint la liste des
	// joueurs en jeu {accountId, characterId, level, zoneId}. Le snapshot est publié à
	// ~1 Hz par TickOnce (thread gameplay) et lu ici de façon thread-safe. La lambda
	// capture gameplayApp par référence : elle vit jusqu'à la fin de main(), comme toMaster.
	toMaster.SetPlayerPresenceProvider([&gameplayApp]() { return gameplayApp.GetPlayerPresenceSnapshot(); });
	// TA.4 : pont position — pool MySQL (meme base que le master, cles db.* du config).
	// Injecte dans ServerApp pour lire characters.spawn_x/y/z au HandleHello. DB non
	// configuree => Init false => spawn depuis le fichier (pont inactif, non bloquant).
	engine::server::db::ConnectionPool characterDbPool;
	if (characterDbPool.Init(config))
	{
		gameplayApp.SetCharacterDbPool(&characterDbPool);
		LOG_INFO(Net, "[ShardMain] Pont position DB actif (spawn depuis characters)");
	}
	else
	{
		LOG_WARN(Net, "[ShardMain] DB non configuree — spawn depuis fichier (pont position TA.4 inactif)");
	}
	std::thread gameplayThread;
	if (gameplayApp.Init())
	{
		gameplayThread = std::thread([&gameplayApp]() { (void)gameplayApp.Run(); });
		LOG_INFO(Net, "[ShardMain] Gameplay UDP loop demarree (ServerApp sur thread dedie)");
	}
	else
	{
		LOG_ERROR(Net, "[ShardMain] ServerApp Init a echoue — replication UDP desactivee");
	}

	// Wave 6 — Instanciation + seed des modules internes (EventAI + Pools).
	// V1 : scripts/pools hardcodes. Future iteration : SeedFromDb() qui
	// remplace SeedV1Events/SeedV1Pools. Le but ici est de prouver que les
	// path tick EventAI / Roll PoolManager sont reellement exerces au
	// runtime, pas seulement testes en unit.
	engine::server::ai::EventAIRuntime eventAi;
	eventAi.SeedV1Events();
	LOG_INFO(AI, "[EventAI] seeded {} V1 events at boot", eventAi.RowCount());

	engine::server::pools::PoolManagerRuntime pools;
	pools.SeedV1Pools();
	LOG_INFO(Pools, "[PoolManager] {} pools registered at boot", pools.PoolCount());

	// Wave 8 — Instanciation + seed ThreatList + DBScripts. Meme pattern
	// que Wave 6 : on prouve que les path tick (decay ThreatList + Step
	// VM DBScripts) sont reellement exerces au runtime, pas seulement en
	// unit tests. Pas de loader DB pour cette PR ; future iteration
	// branche les vraies sources de donnees + dispatch reel des fired
	// commands DBScripts.
	engine::server::combat::ThreatListRuntime threats;
	threats.SeedV1Aggro();
	LOG_INFO(Combat,
		"[ThreatList] {} creature(s) seeded with {} aggro entries at boot",
		threats.CreatureCount(), threats.TotalEntries());

	engine::server::dbscripts::DBScriptRuntime dbScripts;
	dbScripts.SeedV1Scripts();
	LOG_INFO(DBScripts,
		"[DBScripts] {} scripts loaded at boot",
		dbScripts.ScriptCount());

	// Wave 9 — AntiCheat + SpellFamily + InstanceManager. Meme pattern : on
	// seede au boot pour qu'au moins UN cycle V1 soit exerce en prod (et
	// pour qu'un futur SeedFromDb() ait son point d'accroche evident).
	engine::server::anticheat::AntiCheatGameplayRuntime antiCheat;
	antiCheat.SeedV1Config();
	LOG_INFO(AntiCheat, "[AntiCheat] runtime configured (V1 thresholds : maxSpeed=13.0 m/s, tolerance=1.5, maxStep=50 m)");

	engine::server::spell::SpellFamilyRuntime spellFamily;
	spellFamily.SeedV1Families();
	LOG_INFO(Spell, "[SpellFamily] {} spells registered at boot", spellFamily.SpellCount());

	engine::server::maps::InstanceManagerRuntime instanceMgr;
	instanceMgr.SeedV1Maps();
	LOG_INFO(Maps, "[InstanceManager] {} maps registered at boot", instanceMgr.MapCount());

	// Tick periodique EventAI : intervalle 1s. La boucle principale tourne
	// a 100ms (sleep_for(100ms) ci-dessous) donc on filtre via
	// lastEventAiTickMs. Idem pour le log periodique 60s qui dump le
	// cumul "N events fired".
	using clock = std::chrono::steady_clock;
	auto lastEventAiTickTime = clock::now();
	auto lastEventAiLogTime  = clock::now();
	const auto kEventAiTickInterval = std::chrono::milliseconds(1000);
	const auto kEventAiLogInterval  = std::chrono::seconds(60);
	std::uint64_t firesSinceLastLog = 0;

	// Wave 8 — Cadences propres aux deux nouveaux modules :
	//   - ThreatList : decay periodique tous les 5s (combat plus lent
	//     a tick que l'AI). Log periodique 60s dump du cumul purges.
	//   - DBScripts  : Step VM tous les 1s. Log periodique 60s dump du
	//     cumul commandes firees. RunScript(1) une seule fois apres le
	//     seed pour que l'execution V1 fire reellement au runtime, ce
	//     qui valide la chaine complete Start -> Step -> teardown.
	auto lastThreatTickTime  = clock::now();
	auto lastThreatLogTime   = clock::now();
	auto lastDbScriptTickTime = clock::now();
	auto lastDbScriptLogTime  = clock::now();
	const auto kThreatTickInterval   = std::chrono::seconds(5);
	const auto kThreatLogInterval    = std::chrono::seconds(60);
	const auto kDbScriptTickInterval = std::chrono::seconds(1);
	const auto kDbScriptLogInterval  = std::chrono::seconds(60);
	std::uint64_t decaysSinceLastLog       = 0;
	std::uint64_t dbScriptFiresSinceLastLog = 0;

	// Demarre une execution V1 de scriptId=1 ("greet") pour que la
	// boucle Step en ait au moins une a faire avancer. En prod ce
	// RunScript sera declenche par un evenement gameplay (interaction
	// NPC, etc.) plutot que cable inconditionnellement au boot.
	{
		const std::uint64_t bootNowMs = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		if (dbScripts.RunScript(1, bootNowMs))
		{
			LOG_INFO(DBScripts,
				"[DBScripts] V1 script 1 ('greet') started at boot ({} active)",
				dbScripts.ActiveCount());
		}
	}

	// Wave 9 — Cadence AntiCheat : 1Hz (granularite mouvement client). Le
	// log periodique 60s ne dump que si le cumul est > 0 (pas de bruit en
	// regime nominal). cumulativeAntiCheatViolations est volontairement
	// distinct de TotalViolations() pour pouvoir reset par tranche de 60s
	// sans toucher au cumul "depuis boot" expose par le runtime.
	auto lastAntiCheatTickTime = clock::now();
	auto lastAntiCheatLogTime  = clock::now();
	const auto kAntiCheatTickInterval = std::chrono::milliseconds(1000);
	const auto kAntiCheatLogInterval  = std::chrono::seconds(60);
	std::uint64_t cumulativeAntiCheatViolations = 0;

	LOG_INFO(Net, "[ShardMain] Shard server running (Ctrl+C to stop); master {}:{} register endpoint='{}'",
		masterHost, masterPort, regEndpoint);

	while (server.IsRunning() && g_quit == 0)
	{
		// Met a jour le nombre de joueurs connectes (NetServer connection count)
		// avant chaque Pump : ShardToMasterClient::SendHeartbeat utilise cette
		// valeur pour le payload SHARD_HEARTBEAT, qui alimente l'API status
		// (totalPlayers + game_servers[].players). Avant, m_current_load restait
		// a 0 car SetCurrentLoad n'etait jamais appele.
		toMaster.SetCurrentLoad(server.GetConnectionCount());
		toMaster.Pump();

		// Wave 6 — Tick EventAI (1Hz). Convertit steady_clock en wall-clock
		// (system_clock) car les triggers Timer comparent ctx.nowMs avec un
		// timer interne aussi en ms wall-clock.
		const auto nowSteady = clock::now();
		if (nowSteady - lastEventAiTickTime >= kEventAiTickInterval)
		{
			const std::uint64_t realNowMs = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			firesSinceLastLog += eventAi.Tick(realNowMs);
			lastEventAiTickTime = nowSteady;
		}
		if (nowSteady - lastEventAiLogTime >= kEventAiLogInterval)
		{
			LOG_INFO(AI, "[EventAI] tick : {} events fired (last 60s), total since boot {}",
				firesSinceLastLog, eventAi.TotalFires());
			firesSinceLastLog = 0;
			lastEventAiLogTime = nowSteady;
		}

		// Wave 8 — Tick ThreatList (5s). Wall-clock pas indispensable V1
		// (decay par tick), mais on passe nowMs pour parite API et pour
		// futurs scenarios de decay base sur l'inactivite.
		if (nowSteady - lastThreatTickTime >= kThreatTickInterval)
		{
			const std::uint64_t realNowMs = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			decaysSinceLastLog += threats.Tick(realNowMs);
			lastThreatTickTime = nowSteady;
		}
		if (nowSteady - lastThreatLogTime >= kThreatLogInterval)
		{
			LOG_INFO(Combat,
				"[ThreatList] tick : {} entries decayed (last 60s), total since boot {}, currently {} entries across {} creatures",
				decaysSinceLastLog, threats.TotalDecayed(),
				threats.TotalEntries(), threats.CreatureCount());
			decaysSinceLastLog = 0;
			lastThreatLogTime = nowSteady;
		}

		// Wave 8 — Tick DBScripts (1s). nowMs en wall-clock car la VM
		// compare nextRunTickMs (initialise depuis Start) a nowMs.
		if (nowSteady - lastDbScriptTickTime >= kDbScriptTickInterval)
		{
			const std::uint64_t realNowMs = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			dbScriptFiresSinceLastLog += dbScripts.Tick(realNowMs);
			lastDbScriptTickTime = nowSteady;
		}
		if (nowSteady - lastDbScriptLogTime >= kDbScriptLogInterval)
		{
			LOG_INFO(DBScripts,
				"[DBScripts] tick : {} commands fired (last 60s), total since boot {}, {} active",
				dbScriptFiresSinceLastLog, dbScripts.TotalFires(),
				dbScripts.ActiveCount());
			dbScriptFiresSinceLastLog = 0;
			lastDbScriptLogTime = nowSteady;
		}

		// Wave 9 — Tick AntiCheat (1Hz). Meme conversion steady -> wall-clock
		// que pour EventAI : le detecteur calcule un dt en millisecondes
		// wall-clock, donc passer realNowMs est obligatoire.
		if (nowSteady - lastAntiCheatTickTime >= kAntiCheatTickInterval)
		{
			const std::uint64_t realNowMs = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			cumulativeAntiCheatViolations += antiCheat.Tick(realNowMs);
			lastAntiCheatTickTime = nowSteady;
		}
		if (nowSteady - lastAntiCheatLogTime >= kAntiCheatLogInterval)
		{
			if (cumulativeAntiCheatViolations > 0)
			{
				LOG_INFO(AntiCheat,
					"[AntiCheat] {} violations in last 60s (total since boot {})",
					cumulativeAntiCheatViolations, antiCheat.TotalViolations());
				cumulativeAntiCheatViolations = 0;
			}
			lastAntiCheatLogTime = nowSteady;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	// TA.3 : arret propre de la boucle gameplay UDP.
	if (gameplayThread.joinable())
	{
		gameplayApp.RequestStop();
		gameplayThread.join();
	}
	characterDbPool.Shutdown(); LOG_INFO(Net, "[ShardMain] Shutdown");
	healthEndpoint.Shutdown();
	engine::core::Log::Shutdown();
	return 0;
}
