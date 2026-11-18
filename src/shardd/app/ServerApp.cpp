// ServerApp — cœur du serveur de jeu (shard) : construction, Init, boucle Run,
// arrêt, réception et aiguillage des paquets (DispatchGamePacket), tick et
// simulation. Les autres domaines sont dans les fichiers ServerApp<Domaine>.cpp
// (connexion, combat, sorts, quêtes, économie…) ; constantes et utilitaires
// communs dans ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	ServerApp::ServerApp(engine::core::Config config)
		: m_config(std::move(config))
		, m_characterPersistence(m_config)
		, m_auctionHouse(m_config)
		, m_eventRuntime(m_config)
		, m_questRuntime(m_config)
		, m_spawnerRuntime(m_config)
		, m_archetypeLibrary(m_config)
		, m_spellKits(m_config)
		, m_classSkills(m_config)
		, m_combatRng(std::random_device{}())
	{
		LOG_INFO(Core, "[ServerApp] Constructed");
	}

	ServerApp::~ServerApp()
	{
		Shutdown();
	}

	bool ServerApp::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ServerApp] Init ignored: already initialized");
			return true;
		}

		m_listenPort = ClampToU16(m_config.GetInt("server.listen_port", 27015), 1, 65535);
		// ETAPE1.2 — identifiant du shard pour le contrôle de propriété des personnages.
		m_shardId = static_cast<uint32_t>(std::max<int64_t>(0, m_config.GetInt("shard.id", 1)));
		m_tickHz = ResolveTickHz();
		m_snapshotHz = ResolveSnapshotHz(m_tickHz);
		// TG.3 — opt-in split-receive : lecture config au boot. Le flag est lu UNE seule fois
		// (changer le mode en cours d'exécution casserait l'invariant queue-vide↔mono-thread).
		m_splitReceiveThread = m_config.GetBool("server.network.split_receive_thread", false);
		if (m_splitReceiveThread)
		{
			LOG_INFO(Core, "[ServerApp] Split-receive thread actif (TG.3) : I/O reseau decorrele du thread tick");
		}
		// TG.4 — cap top-N par distance configurable (0 = pas de cap). Bornage défensif :
		// on clamp a un plafond confortable pour éviter qu'une faute de config (-1, valeur
		// énorme) ne casse silencieusement la replication ou ne sature la mémoire au boot.
		{
			// ETAPE3.2 (chantier 4.5) — défaut 100 entités par client (0 = pas de plafond).
			const int64_t cfgViewCap = m_config.GetInt("server.replication.view_cap_entities", 100);
			// ETAPE3.2 — borne = plafond du codec des snapshots (kMaxEntitiesPerSnapshot) :
			// au-delà, le codec couperait sans tenir compte des priorités (groupe, cible).
			// 0 (pas de plafond) devient donc ce maximum.
			constexpr int64_t kMaxReasonableViewCap =
				static_cast<int64_t>(engine::network::snapshot::kMaxEntitiesPerSnapshot);
			const int64_t clampedViewCap = (cfgViewCap <= 0)
				? kMaxReasonableViewCap
				: std::clamp<int64_t>(cfgViewCap, 1, kMaxReasonableViewCap);
			m_viewCapEntities = static_cast<uint32_t>(clampedViewCap);
			m_viewCapNominal = m_viewCapEntities; // ETAPE4.5 : valeur hors délestage
			if (m_viewCapEntities != 0u)
			{
				LOG_INFO(Core, "[ServerApp] View cap actif (TG.4) : top-{} entites/client par snapshot", m_viewCapEntities);
			}
		}
		m_nextClientId = 1;
		m_currentTick = 0;
		m_characterAutosaveIntervalTicks = 0;
		m_nextCharacterAutosaveTick = 0;
		m_snapshotAccumulator = 0;
		m_stopRequested = false;
		m_clients.clear();
		// TG.2 — vider aussi les 3 index pour rester cohérent.
		m_clientIndexByPackedEndpoint.clear();
		m_clientIndexByClientId.clear();
		m_clientIndexByEntityId.clear();
		m_mobs.clear();
		m_lootBags.clear();
		m_lootTableEntries.clear();
		m_dynamicEvents.clear();
		m_spawners.clear();
		m_pendingDatagrams.clear();
		m_pendingHellos.clear(); // TA.3 — buffer Hello en attente d'admission, vide au boot.
		// ETAPE3.1 — chiffrement des datagrammes de jeu. Défaut : obligatoire dès qu'un
		// registre d'admission est branché (shard Linux : la clé arrive avec l'admission),
		// facultatif sinon (build Windows sans master).
		m_udpSessions.Clear();
		m_udpRejectedDatagrams = 0;
		const std::string encryptionMode = m_config.GetString("gameplay_udp.encryption", "");
		if (encryptionMode == "required")
			m_udpEncryptionMode = UdpEncryptionMode::Required;
		else if (encryptionMode == "optional")
			m_udpEncryptionMode = UdpEncryptionMode::Optional;
		else
		{
			if (!encryptionMode.empty())
				LOG_WARN(Core, "[ServerApp] gameplay_udp.encryption='{}' inconnu (attendu : required | optional) : défaut appliqué",
					encryptionMode);
			m_udpEncryptionMode = (m_admittedRegistry != nullptr) ? UdpEncryptionMode::Required : UdpEncryptionMode::Optional;
		}
		LOG_INFO(Core, "[ServerApp] Chiffrement UDP : {}",
			m_udpEncryptionMode == UdpEncryptionMode::Required ? "obligatoire" : "facultatif");
		m_zoneGrids.clear();
		m_nextServerEntityId = 0x200000000ull;
		if (!m_zoneTransitionMap.Init())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: zone transition map startup failed");
			return false;
		}

		if (!m_transport.Init(m_listenPort))
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: transport startup failed");
			m_zoneTransitionMap.Shutdown();
			return false;
		}

		if (!m_tickScheduler.Init(m_tickHz))
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: tick scheduler startup failed");
			m_transport.Shutdown();
			m_zoneTransitionMap.Shutdown();
			return false;
		}

		if (!m_characterPersistence.Init())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: character persistence startup failed");
			Shutdown();
			return false;
		}

#if defined(SHARD_POSITION_DB)
		// ETAPE2.2 — sauvegardes hors du tick, seulement avec une base. Chaque thread
		// garde une connexion du pool pendant une écriture : laisser au moins une
		// connexion au tick (chargements au Hello). Clés optionnelles :
		//   shard.save_workers (défaut 2), shard.save_queue_max (défaut 4096).
		// Dimensionné sur les connexions réellement ouvertes (pas sur db.pool_size) :
		// avec moins de 2 connexions, pas de file (écritures dans le tick, comme avant).
		const size_t liveConnections = (m_characterDbPool != nullptr && m_characterDbPool->IsInitialized())
			? m_characterDbPool->ConnectedCount() : 0u;
		if (liveConnections == 1u)
		{
			LOG_WARN(Core, "[ServerApp] Character save queue disabled: only 1 DB connection open (db.pool_size)");
		}
		if (liveConnections >= 2u)
		{
			const size_t workers = static_cast<size_t>(std::clamp<int64_t>(
				m_config.GetInt("shard.save_workers", 2), 1, static_cast<int64_t>(liveConnections) - 1));
			const size_t maxPending = static_cast<size_t>(std::clamp<int64_t>(
				m_config.GetInt("shard.save_queue_max", 4096), 64, 1000000));
			m_characterSaveQueue = std::make_unique<CharacterSaveQueue>(workers, maxPending,
				[] { mysql_thread_init(); }, [] { mysql_thread_end(); });
			m_characterSaveQueue->Start();
			LOG_INFO(Core, "[ServerApp] Character save queue started (workers={}, max_pending={})", workers, maxPending);
		}
#endif

		if (!m_currencyConfig.Load(m_config))
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: currency config load failed");
			Shutdown();
			return false;
		}

		if (!m_vendorCatalog.Load(m_config))
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: vendor catalog load failed");
			Shutdown();
			return false;
		}
		m_vendorStock.ResetFromCatalog(m_vendorCatalog.GetVendors());

		// Chantier 2 SP-A — catalogue d'objets (autoritaire pour slot/bonus). Non
		// fatal : un catalogue absent désactive simplement l'équipement (pas de
		// régression sur le reste du gameplay), à l'image de m_statsTables.
		{
			const std::string itemsPath = m_config.GetString("server.items_catalog_path", "items/items.json");
			const std::string itemsText = engine::platform::FileSystem::ReadAllTextContent(m_config, itemsPath);
			if (itemsText.empty() || !m_itemCatalog.LoadFromJson(itemsText))
			{
				LOG_WARN(Core,
					"[ServerApp] catalogue d'objets absent/illisible (path={}) — équipement désactivé",
					itemsPath);
			}
			else
			{
				m_itemCatalogLoaded = true;
				LOG_INFO(Core, "[ServerApp] catalogue d'objets chargé : {} objet(s)", m_itemCatalog.Count());
			}
		}

		m_characterAutosaveIntervalTicks = ResolveCharacterAutosaveIntervalTicks();
		m_nextCharacterAutosaveTick = m_characterAutosaveIntervalTicks;

		if (!InitLootTables())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: loot tables startup failed");
			Shutdown();
			return false;
		}
		// Monde « Royaumes » — monde en cellules : départs par race, cellules générées.
		if (!InitCellWorld())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: cell world data missing (world.active_zone=royaumes)");
			Shutdown();
			return false;
		}

		// Validation v12 (wire v13) — points de réapparition (non bloquant).
		InitRespawnPoints();
		InitDiscoveryCatalog(); // lore v1.0.114 — après les points de réapparition
		InitLavaHazard();       // v1.0.123 — lave mortelle des Enfers

		if (!InitQuests())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: quest runtime startup failed");
			Shutdown();
			return false;
		}

		if (!InitSpawners())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: spawner bootstrap failed");
			Shutdown();
			return false;
		}

		// Combat SP3 — kits de sorts par profil (strict : kit corrompu = pas de boot).
		if (!m_spellKits.Init())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: spell kit library startup failed");
			Shutdown();
			return false;
		}

		// SP-B — compétences par-classe (strict : fichier corrompu = pas de boot).
		if (!m_classSkills.Init())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: class skill library startup failed");
			Shutdown();
			return false;
		}

		if (!InitDynamicEvents())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: dynamic event bootstrap failed");
			Shutdown();
			return false;
		}

		m_tickScheduler.Start();
		m_chatRateLimiter.Reset();
		InitModerationAuditSubsystem();
		LoadChatBanFile();

		// M32.1 — FriendSystem runs in no-DB mode on WIN32 (presence tracking only).
		// FIX.4 — shard Linux avec base (pool) : amis persistés dans la table `friends`,
		// opérations SQL déposées dans la file de sauvegarde (jamais sur le tick).
		m_friendSystem.Init(m_characterDbPool != nullptr);
		// Présence unifiée : FriendSystem consulte l'autorité unique (par character_id)
		// au lieu de tenir sa propre map de présence.
		m_friendSystem.SetPresenceService(&m_shardPresence);

		// M32.2 — PartySystem (in-memory, no DB dependency).
		m_partySystem.Init();

		// M35.3 — TradeSystem (in-memory, direct player-to-player trade).
		m_tradeSystem.Init(m_tickHz);

		// M36.1 — GatheringSystem (resource nodes + harvest sessions).
		if (!InitGathering())
		{
			LOG_WARN(Core, "[ServerApp] GatheringSystem init skipped or partial — harvesting unavailable");
		}

		// M36.2 — CraftingSystem (recipes + profession skill leveling).
		if (!InitCrafting())
		{
			LOG_WARN(Core, "[ServerApp] CraftingSystem init skipped or partial — crafting unavailable");
		}

#if defined(SHARD_POSITION_DB)
		// ETAPE2.8 — avec une base, les annonces sont dans auction_listings.
		if (m_characterDbPool != nullptr)
		{
			m_auctionHouse.SetFilePersistence(false);
		}
#endif
		if (!m_auctionHouse.Init())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: auction house startup failed");
			Shutdown();
			return false;
		}
		LoadAuctionListingsFromDb(); // ETAPE2.8 (sans effet sans base)

		m_initialized = true;
		LOG_INFO(Core, "[ServerApp] Init OK (port={}, tick_hz={}, snapshot_hz={})",
			m_listenPort, m_tickHz, m_snapshotHz);
		LOG_INFO(Net, "[ServerApp] Chat routing ready (rate_limit_msgs_per_sec={})",
			engine::net::ChatRateLimiter::kMaxMessagesPerSecond);
		return true;
	}

	int ServerApp::Run()
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ServerApp] Run FAILED: app not initialized");
			return 1;
		}

		LOG_INFO(Core, "[ServerApp] Run loop started");

		// TG.3 — démarre le thread reseau si le mode split est actif (opt-in config).
		if (m_splitReceiveThread)
		{
			m_networkThreadStopRequested.store(false, std::memory_order_relaxed);
			m_networkThread = std::thread([this]() { NetworkPumpLoop(); });
		}

		while (!m_stopRequested)
		{
			if (!m_tickScheduler.WaitForNextTick())
			{
				LOG_ERROR(Core, "[ServerApp] Run FAILED: scheduler tick wait failed");
				// TG.3 — signaler l'arret au thread reseau avant de remonter l'erreur
				// (sinon il continuerait a appeler m_transport.Receive apres Shutdown).
				if (m_networkThread.joinable())
				{
					m_networkThreadStopRequested.store(true, std::memory_order_relaxed);
					m_networkThread.join();
				}
				return 1;
			}

			// ETAPE4.5 (chantier 2.5) — durée réelle du tick (réception + simulation +
			// envois) : déclencheur du délestage.
			const auto tickStart = std::chrono::steady_clock::now();
			// ETAPE5.2 (chantier 1.5) — un écart anormal depuis le tick précédent (tick très
			// lent, pause de la machine) suspend l'éviction des clients UDP inactifs : leurs
			// paquets attendaient dans la socket, ce ne sont pas des zombies.
			if (m_stallGuard.OnLoop(std::chrono::duration<double>(tickStart.time_since_epoch()).count()))
			{
				LOG_WARN(Core, "[ServerApp] ETAPE5.2 calage du tick ({:.1f} s, total={}) : éviction UDP suspendue {:.0f} s",
					m_stallGuard.LastStallSeconds(), m_stallGuard.StallCount(), m_stallGuard.Config().graceSeconds);
			}
			ProcessIncomingPackets();
			TickOnce();
			const auto tickEnd = std::chrono::steady_clock::now();
			const double tickSeconds = std::chrono::duration<double>(tickEnd - tickStart).count();
			// ETAPE5.4 — alimente les métriques (dernier tick, pire tick de la seconde).
			m_lastTickSeconds = tickSeconds;
			m_tickMaxSinceMetrics = (std::max)(m_tickMaxSinceMetrics, tickSeconds);
			const double nowSeconds = std::chrono::duration<double>(tickEnd.time_since_epoch()).count();
			const SheddingTransition shed = m_loadShedder.OnTick(tickSeconds,
				1.0 / static_cast<double>(std::max<int64_t>(m_tickHz, 1)), nowSeconds);
			if (shed == SheddingTransition::Entered)
				ApplyLoadShedding(true);
			else if (shed == SheddingTransition::Exited)
				ApplyLoadShedding(false);
		}

		// TG.3 — arret propre du thread reseau apant la sortie de Run.
		if (m_networkThread.joinable())
		{
			m_networkThreadStopRequested.store(true, std::memory_order_relaxed);
			m_networkThread.join();
		}

		LOG_INFO(Core, "[ServerApp] Run loop stopped");
		return 0;
	}

	void ServerApp::RequestStop()
	{
		m_stopRequested = true;
		LOG_INFO(Core, "[ServerApp] Stop requested");
	}

	void ServerApp::Shutdown()
	{
		m_chatRateLimiter.Reset();

		if (!m_initialized
			&& !m_transport.IsValid()
			&& m_clients.empty()
			&& m_mobs.empty()
			&& m_lootBags.empty()
			&& m_lootTableEntries.empty()
			&& m_zoneGrids.empty()
			&& m_pendingDatagrams.empty()
			&& !m_networkThread.joinable()) // TG.3 — pas de thread reseau pendant
		{
			m_moderationAuditLog.Shutdown();
			m_moderationAuditLogReady = false;
			return;
		}

		m_initialized = false;
		// TG.3 — defensive : si Shutdown est appele en dehors de Run (ex. erreur Init),
		// s'assurer que le thread reseau est bien arrete et joine.
		if (m_networkThread.joinable())
		{
			m_networkThreadStopRequested.store(true, std::memory_order_relaxed);
			m_networkThread.join();
		}
		{
			std::lock_guard<std::mutex> lock(m_networkQueueMutex);
			m_networkIngressQueue.clear();
		}
		for (const ConnectedClient& client : m_clients)
		{
			SaveConnectedClient(client, "shutdown");
			RecordCharacterSnapshot(client, "shutdown"); // ETAPE2.6
			if (client.hasCell)
			{
				const auto zoneIt = m_zoneGrids.find(engine::world::cells::MapIdOfZone(client.zoneId));
				if (zoneIt != m_zoneGrids.end())
				{
					(void)zoneIt->second.RemoveEntity(client.entityId);
				}
			}
		}
		// ETAPE2.2 — écrit tout ce qui reste dans la file de sauvegarde (y compris
		// les sauvegardes « shutdown » ci-dessus) avant de continuer l'arrêt.
		if (m_characterSaveQueue != nullptr)
		{
			const auto stats = m_characterSaveQueue->GetStats();
			m_characterSaveQueue->Stop();
			LOG_INFO(Net, "[ServerApp] Character save queue stopped (queued={}, coalesced={}, rejected={}, executed={})",
				stats.queued, stats.coalesced, stats.rejected, m_characterSaveQueue->GetStats().executed);
			m_characterSaveQueue.reset();
		}
		// ETAPE2.7 — Hello en cours de lecture abandonnés (le client se reconnectera).
		m_pendingCharacterLoads.clear();
		{
			std::lock_guard<std::mutex> lock(m_completedCharacterLoadsMutex);
			m_completedCharacterLoads.clear();
		}
		m_clients.clear();
		// TG.2 — Shutdown : vider aussi les 3 index O(1) sur m_clients.
		m_clientIndexByPackedEndpoint.clear();
		m_clientIndexByClientId.clear();
		m_clientIndexByEntityId.clear();
		for (const MobEntity& mob : m_mobs)
		{
			const auto zoneIt = m_zoneGrids.find(engine::world::cells::MapIdOfZone(mob.zoneId));
			if (zoneIt != m_zoneGrids.end())
			{
				(void)zoneIt->second.RemoveEntity(mob.entityId);
			}
		}
		m_mobs.clear();
		for (const LootBagEntity& lootBag : m_lootBags)
		{
			const auto zoneIt = m_zoneGrids.find(engine::world::cells::MapIdOfZone(lootBag.zoneId));
			if (zoneIt != m_zoneGrids.end())
			{
				(void)zoneIt->second.RemoveEntity(lootBag.entityId);
			}
		}
		m_lootBags.clear();
		m_lootTableEntries.clear();
		for (auto& [zoneId, zoneGrid] : m_zoneGrids)
		{
			(void)zoneId;
			zoneGrid.Shutdown();
		}
		m_zoneGrids.clear();
		m_pendingDatagrams.clear();
		m_pendingHellos.clear(); // TA.3 — buffer Hello en attente d'admission.
		m_tickScheduler.Shutdown();
		m_transport.Shutdown();
		m_zoneTransitionMap.Shutdown();
		m_spawnerRuntime.Shutdown();
		m_eventRuntime.Shutdown();
		m_questRuntime.Shutdown();
		m_auctionHouse.Shutdown();
		m_characterPersistence.Shutdown();
		m_dynamicEvents.clear();
		m_spawners.clear();
		// ETAPE4.3 — index et listes qui pointent dans m_spawners.
		m_spawnersByRegion.clear();
		m_spawnersOutsideCells.clear();
		m_evaluatedSpawners.clear();
		m_evaluatedRegions.clear();
		m_regionLifecycle.Clear();
		m_moderationAuditLog.Shutdown();
		m_moderationAuditLogReady = false;
		m_friendSystem.Shutdown();
		m_partySystem.Shutdown();
		m_tradeSystem.Shutdown();
		if (m_gatheringSystem.IsInitialized())
			m_gatheringSystem.Shutdown();
		if (m_craftingSystem.IsInitialized())
			m_craftingSystem.Shutdown();
		LOG_INFO(Core, "[ServerApp] Destroyed");
	}

	uint16_t ServerApp::ResolveTickHz() const
	{
		const uint16_t configuredTickHz = ClampToU16(m_config.GetInt("server.tick_hz", 20), 1, 120);
		if (configuredTickHz == 20 || configuredTickHz == 30)
		{
			return configuredTickHz;
		}

		LOG_WARN(Core, "[ServerApp] Unsupported server.tick_hz={} -> fallback to 20", configuredTickHz);
		return 20;
	}

	uint16_t ServerApp::ResolveSnapshotHz(uint16_t tickHz) const
	{
		const uint16_t configuredSnapshotHz = ClampToU16(m_config.GetInt("server.snapshot_hz", 10), 1, tickHz);
		const uint16_t clampedSnapshotHz = std::clamp<uint16_t>(configuredSnapshotHz, 10, tickHz);
		if (clampedSnapshotHz > 20)
		{
			LOG_WARN(Core, "[ServerApp] server.snapshot_hz={} above ticket range -> clamp to 20", clampedSnapshotHz);
			return 20;
		}
		if (clampedSnapshotHz != configuredSnapshotHz)
		{
			LOG_WARN(Core, "[ServerApp] server.snapshot_hz={} adjusted to {}", configuredSnapshotHz, clampedSnapshotHz);
		}
		return clampedSnapshotHz;
	}

	uint32_t ServerApp::ResolveCharacterAutosaveIntervalTicks() const
	{
		const uint32_t autosaveSeconds = static_cast<uint32_t>(std::max<int64_t>(1, m_config.GetInt("server.character_autosave_seconds", 30)));
		const uint32_t autosaveTicks = autosaveSeconds * static_cast<uint32_t>(m_tickHz);
		return std::max<uint32_t>(1u, autosaveTicks);
	}

	void ServerApp::NetworkPumpLoop()
	{
		// TG.3 — boucle dediee au thread reseau : draine la socket en continu et empile
		// les datagrammes dans m_networkIngressQueue sous mutex pour le thread tick.
		// Sleep court (1 ms) quand la socket est vide pour eviter de saturer le CPU.
		std::vector<Datagram> localBuffer;
		while (!m_networkThreadStopRequested.load(std::memory_order_relaxed))
		{
			localBuffer.clear();
			const size_t received = m_transport.Receive(localBuffer, 128);
			if (received > 0u)
			{
				std::lock_guard<std::mutex> lock(m_networkQueueMutex);
				m_networkIngressQueue.insert(
					m_networkIngressQueue.end(),
					std::make_move_iterator(localBuffer.begin()),
					std::make_move_iterator(localBuffer.end()));
			}
			else
			{
				// Socket vide : libere le CPU en attendant le prochain datagramme.
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	void ServerApp::ProcessIncomingPackets()
	{
		// TG.3 — mode split actif : on draine la queue remplie par le thread reseau (swap
		// minimise la duree de detention du mutex). Mode mono-thread : appel direct comme avant.
		if (m_splitReceiveThread)
		{
			m_pendingDatagrams.clear();
			{
				std::lock_guard<std::mutex> lock(m_networkQueueMutex);
				std::swap(m_pendingDatagrams, m_networkIngressQueue);
			}
		}
		else
		{
			m_transport.Receive(m_pendingDatagrams, 64);
		}
		for (const Datagram& datagram : m_pendingDatagrams)
		{
			ProcessPacket(datagram);
		}
	}

	void ServerApp::ProcessPacket(const Datagram& datagram)
	{
		// ETAPE3.1 — enveloppe chiffrée : poignée de main (Hello) ou données. Le paquet de
		// jeu déchiffré repasse par DispatchGamePacket, exactement comme un paquet en clair.
		const std::span<const std::byte> bytes(datagram.bytes.data(), datagram.size);
		if (engine::network::IsSecureDatagram(bytes))
		{
			if (bytes.size() > 4u && static_cast<uint8_t>(bytes[4]) == engine::network::kUdpSecureTypeHandshake)
			{
				ProcessSecureHandshake(datagram.endpoint, bytes);
				return;
			}
			Datagram plain{};
			plain.endpoint = datagram.endpoint;
			std::vector<std::byte> decrypted;
			const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
			const bool opened = m_udpSessions.OpenData(PackEndpointKey(datagram.endpoint), bytes, nowMs, decrypted)
				&& decrypted.size() <= plain.bytes.size();
			if (opened)
			{
				std::copy(decrypted.begin(), decrypted.end(), plain.bytes.begin());
				plain.size = decrypted.size();
			}
			if (!opened)
			{
				// Session inconnue, datagramme falsifié, rejoué ou trop ancien : abandon
				// silencieux (journal 1 fois sur 256 pour ne pas noyer les logs sous un flood).
				if ((m_udpRejectedDatagrams++ & 0xFFu) == 0u)
				{
					LOG_WARN(Net, "[ServerApp] UDP chiffré refusé (endpoint={}, total_refusés={})",
						UdpTransport::EndpointToString(datagram.endpoint), m_udpRejectedDatagrams);
				}
				return;
			}
			DispatchGamePacket(plain);
			return;
		}

		// Datagramme en clair : refusé en mode « required » et pour tout point de
		// terminaison qui a déjà une session chiffrée (pas d'injection en clair dans une
		// session authentifiée).
		const bool endpointHasSession = m_udpSessions.HasSession(PackEndpointKey(datagram.endpoint));
		if (m_udpEncryptionMode == UdpEncryptionMode::Required || endpointHasSession)
		{
			if ((m_udpRejectedDatagrams++ & 0xFFu) == 0u)
			{
				LOG_WARN(Net, "[ServerApp] UDP en clair refusé (endpoint={}, mode={}, session={}, total_refusés={})",
					UdpTransport::EndpointToString(datagram.endpoint),
					m_udpEncryptionMode == UdpEncryptionMode::Required ? "required" : "optional",
					endpointHasSession ? "oui" : "non", m_udpRejectedDatagrams);
			}
			return;
		}
		DispatchGamePacket(datagram);
	}

	void ServerApp::DispatchGamePacket(const Datagram& datagram)
	{
		const std::span<const std::byte> packetBytes(datagram.bytes.data(), datagram.size);

		// ETAPE3.4 — enveloppes du canal fiable (messages numérotés, accusés).
		if (engine::server::MessageKind envelopeKind{};
			engine::server::PeekMessageKind(packetBytes, envelopeKind)
			&& (envelopeKind == engine::server::MessageKind::Reliable
				|| envelopeKind == engine::server::MessageKind::ReliableAck))
		{
			HandleReliableEnvelope(datagram, envelopeKind);
			return;
		}

		HelloMessage hello{};
		if (DecodeHello(packetBytes, hello))
		{
			// ETAPE3.1 — un Hello qui arrive ici est en clair (le Hello chiffré passe par
			// ProcessSecureHandshake, qui appelle HandleHello lui-même).
			HandleHello(datagram.endpoint, hello.clientNonce, false);
			return;
		}

		InputMessage input{};
		if (DecodeInput(packetBytes, input))
		{
			// ETAPE3.2 — acquittement du dernier snapshot reconstruit par le client (pris
			// en compte même si la position est ensuite refusée par l'anti-triche).
			if (ConnectedClient* acking = FindClient(datagram.endpoint);
				acking != nullptr && acking->clientId == input.clientId)
			{
				acking->snapshotSender.OnAck(input.ackSnapshotSeq);
			}
			HandleInput(datagram.endpoint, input);
			return;
		}

		// Départ propre du client : éviction immédiate (sinon l'avatar resterait un fantôme
		// visible des autres jusqu'au timeout d'inactivité). Cf. CODEBASE_MAP §60.
		GoodbyeMessage goodbye{};
		if (DecodeGoodbye(packetBytes, goodbye))
		{
			HandleGoodbye(datagram.endpoint, goodbye.clientId);
			return;
		}

		AttackRequestMessage attackRequest{};
		if (DecodeAttackRequest(packetBytes, attackRequest))
		{
			HandleAttackRequest(datagram.endpoint, attackRequest.clientId, attackRequest.targetEntityId);
			return;
		}

		// v1.0.125 — embarquer sur un bateau à quai / en débarquer.
		BoatBoardRequestMessage boatBoard{};
		if (DecodeBoatBoardRequest(packetBytes, boatBoard))
		{
			HandleBoatBoardRequest(datagram.endpoint, boatBoard);
			return;
		}

		// Combat SP2 — réapparition d'un joueur mort (v13 : destination typée).
		RespawnRequestMessage respawnRequest{};
		if (DecodeRespawnRequest(packetBytes, respawnRequest))
		{
			HandleRespawnRequest(datagram.endpoint, respawnRequest.clientId, respawnRequest.destination);
			return;
		}

		// Combat SP3 — cast d'un sort du kit du profil.
		CastRequestMessage castRequest{};
		if (DecodeCastRequest(packetBytes, castRequest))
		{
			HandleCastRequest(datagram.endpoint, castRequest);
			return;
		}

		// Grimoire — réassignation des slots de barre d'action.
		SetActionBarLayoutMessage setLayout{};
		if (DecodeSetActionBarLayout(packetBytes, setLayout))
		{
			HandleSetActionBarLayout(datagram.endpoint, setLayout);
			return;
		}

		// Roadmap-3 (2026-07-19) — Ceinture : réassignation des slots (kind 99).
		SetBeltLayoutMessage setBelt{};
		if (DecodeSetBeltLayout(packetBytes, setBelt))
		{
			HandleSetBeltLayout(datagram.endpoint, setBelt);
			return;
		}

		// SP-B — choix d'une compétence par-classe (kind 91).
		ChooseClassSkillRequestMessage chooseSkill{};
		if (DecodeChooseClassSkillRequest(packetBytes, chooseSkill))
		{
			HandleChooseClassSkill(datagram.endpoint, chooseSkill);
			return;
		}

		PickupRequestMessage pickupRequest{};
		if (DecodePickupRequest(packetBytes, pickupRequest))
		{
			HandlePickupRequest(datagram.endpoint, pickupRequest.clientId, pickupRequest.lootBagEntityId);
			return;
		}

		// Chantier 2 SP-A — équiper / déséquiper un objet.
		EquipRequestMessage equipRequest{};
		if (DecodeEquipRequest(packetBytes, equipRequest))
		{
			HandleEquipRequest(datagram.endpoint, equipRequest.clientId, equipRequest.itemId);
			return;
		}

		UnequipRequestMessage unequipRequest{};
		if (DecodeUnequipRequest(packetBytes, unequipRequest))
		{
			HandleUnequipRequest(datagram.endpoint, unequipRequest.clientId, unequipRequest.slot);
			return;
		}

		TalkRequestMessage talkRequest{};
		if (DecodeTalkRequest(packetBytes, talkRequest))
		{
			HandleTalkRequest(datagram.endpoint, talkRequest.clientId, talkRequest.targetId);
			return;
		}

		// SP1 quêtes — accepter une quête proposée par un PNJ giver.
		QuestAcceptRequestMessage questAccept{};
		if (DecodeQuestAcceptRequest(packetBytes, questAccept))
		{
			HandleAcceptQuest(datagram.endpoint, questAccept);
			return;
		}

		// SP1 quêtes — rendre une quête (récompense versée ici).
		QuestTurnInRequestMessage questTurnIn{};
		if (DecodeQuestTurnInRequest(packetBytes, questTurnIn))
		{
			HandleTurnInQuest(datagram.endpoint, questTurnIn);
			return;
		}

		ShopBuyRequestMessage shopBuy{};
		if (DecodeShopBuyRequest(packetBytes, shopBuy))
		{
			HandleShopBuyRequest(datagram.endpoint, shopBuy);
			return;
		}

		ShopSellRequestMessage shopSell{};
		if (DecodeShopSellRequest(packetBytes, shopSell))
		{
			HandleShopSellRequest(datagram.endpoint, shopSell);
			return;
		}

		AuctionBrowseRequestMessage ahBrowse{};
		if (DecodeAuctionBrowseRequest(packetBytes, ahBrowse))
		{
			HandleAuctionBrowseRequest(datagram.endpoint, ahBrowse);
			return;
		}

		AuctionListItemRequestMessage ahList{};
		if (DecodeAuctionListItemRequest(packetBytes, ahList))
		{
			HandleAuctionListItemRequest(datagram.endpoint, ahList);
			return;
		}

		AuctionBidRequestMessage ahBid{};
		if (DecodeAuctionBidRequest(packetBytes, ahBid))
		{
			HandleAuctionBidRequest(datagram.endpoint, ahBid);
			return;
		}

		AuctionBuyoutRequestMessage ahBuy{};
		if (DecodeAuctionBuyoutRequest(packetBytes, ahBuy))
		{
			HandleAuctionBuyoutRequest(datagram.endpoint, ahBuy);
			return;
		}

		// M35.3 — Direct player-to-player trade packets.
		TradeAcceptMessage tradeAccept{};
		if (DecodeTradeAccept(packetBytes, tradeAccept))
		{
			HandleTradeAccept(datagram.endpoint, tradeAccept);
			return;
		}

		TradeDeclineMessage tradeDecline{};
		if (DecodeTradeDecline(packetBytes, tradeDecline))
		{
			HandleTradeDecline(datagram.endpoint, tradeDecline);
			return;
		}

		TradeAddItemMessage tradeAddItem{};
		if (DecodeTradeAddItem(packetBytes, tradeAddItem))
		{
			HandleTradeAddItem(datagram.endpoint, tradeAddItem);
			return;
		}

		TradeSetGoldMessage tradeSetGold{};
		if (DecodeTradeSetGold(packetBytes, tradeSetGold))
		{
			HandleTradeSetGold(datagram.endpoint, tradeSetGold);
			return;
		}

		TradeLockMessage tradeLock{};
		if (DecodeTradeLock(packetBytes, tradeLock))
		{
			HandleTradeLock(datagram.endpoint, tradeLock);
			return;
		}

		TradeConfirmMessage tradeConfirm{};
		if (DecodeTradeConfirm(packetBytes, tradeConfirm))
		{
			HandleTradeConfirm(datagram.endpoint, tradeConfirm);
			return;
		}

		TradeCancelMessage tradeCancelMsg{};
		if (DecodeTradeCancel(packetBytes, tradeCancelMsg))
		{
			HandleTradeCancelPacket(datagram.endpoint, tradeCancelMsg);
			return;
		}

		// M36.1 — Gathering / harvesting resource node packets.
		HarvestRequestMessage harvestReq{};
		if (DecodeHarvestRequest(packetBytes, harvestReq))
		{
			HandleHarvestRequest(datagram.endpoint, harvestReq);
			return;
		}

		HarvestCancelRequestMessage harvestCancelReq{};
		if (DecodeHarvestCancelRequest(packetBytes, harvestCancelReq))
		{
			HandleHarvestCancelRequest(datagram.endpoint, harvestCancelReq);
			return;
		}

		// M36.2 — Crafting / profession packets.
		LearnProfessionRequestMessage learnProfReq{};
		if (DecodeLearnProfessionRequest(packetBytes, learnProfReq))
		{
			HandleLearnProfessionRequest(datagram.endpoint, learnProfReq);
			return;
		}

		CraftRecipeListRequestMessage recipeListReq{};
		if (DecodeCraftRecipeListRequest(packetBytes, recipeListReq))
		{
			HandleCraftRecipeListRequest(datagram.endpoint, recipeListReq);
			return;
		}

		CraftRequestMessage craftReq{};
		if (DecodeCraftRequest(packetBytes, craftReq))
		{
			HandleCraftRequest(datagram.endpoint, craftReq);
			return;
		}

		CraftCancelRequestMessage craftCancelReq{};
		if (DecodeCraftCancelRequest(packetBytes, craftCancelReq))
		{
			HandleCraftCancelRequest(datagram.endpoint, craftCancelReq);
			return;
		}

		ChatSendRequestMessage chatSend{};
		if (DecodeChatSend(packetBytes, chatSend))
		{
			HandleChatSend(datagram.endpoint, chatSend);
			return;
		}

		// M32.1 — Friend request packets (client-initiated via dedicated packet type).
		FriendRequestMessage friendReq{};
		if (DecodeFriendRequest(packetBytes, friendReq))
		{
			// FIX.4 — même chemin que /friend (base hors tick, listes rafraîchies).
			if (ConnectedClient* sender = FindClient(datagram.endpoint))
				HandleFriendAction(*sender, FriendSubCommand::Add, friendReq.targetName, false);
			return;
		}

		FriendAcceptMessage friendAccept{};
		if (DecodeFriendAccept(packetBytes, friendAccept))
		{
			// FIX.4 — même chemin que /friend (base hors tick, listes rafraîchies).
			if (ConnectedClient* sender = FindClient(datagram.endpoint))
				HandleFriendAction(*sender, FriendSubCommand::Accept, friendAccept.requesterName, false);
			return;
		}

		FriendDeclineMessage friendDecline{};
		if (DecodeFriendDecline(packetBytes, friendDecline))
		{
			// FIX.4 — même chemin que /friend (base hors tick, listes rafraîchies).
			if (ConnectedClient* sender = FindClient(datagram.endpoint))
				HandleFriendAction(*sender, FriendSubCommand::Decline, friendDecline.requesterName, false);
			return;
		}

		FriendRemoveMessage friendRemove{};
		if (DecodeFriendRemove(packetBytes, friendRemove))
		{
			// FIX.4 — même chemin que /friend (base hors tick, listes rafraîchies).
			if (ConnectedClient* sender = FindClient(datagram.endpoint))
				HandleFriendAction(*sender, FriendSubCommand::Remove, friendRemove.friendName, false);
			return;
		}

		// M32.2 — Party accept packet (invitee replies to a pending invite).
		PartyAcceptMessage partyAccept{};
		if (DecodePartyAccept(packetBytes, partyAccept))
		{
			ConnectedClient* sender = FindClient(datagram.endpoint);
			if (sender && m_partySystem.IsInitialized())
			{
				const std::string inviteeLabel = "P" + std::to_string(sender->clientId);
				const uint32_t partyId = m_partySystem.AcceptInvite(
				    sender->clientId,
				    inviteeLabel,
				    sender->entityId,
				    m_currentTick);
				if (partyId != 0)
				{
					const Party* party = m_partySystem.FindPartyById(partyId);
					if (party != nullptr)
					{
						BroadcastPartyUpdate(*party);
						// Notify all members.
						for (const PartyMember& m : party->members)
						{
							ConnectedClient* member = FindClientByEntityId(m.entityId);
							if (member != nullptr)
								SendChatSystemNotice(*member, inviteeLabel + " has joined the party.");
						}
					}
					LOG_INFO(Net, "[ServerApp] Party invite accepted via packet (client_id={}, party_id={})",
					    sender->clientId, partyId);
				}
				else
				{
					SendChatSystemNotice(*sender, "Party invite expired or invalid.");
					LOG_WARN(Net, "[ServerApp] Party invite accept failed (client_id={})", sender->clientId);
				}
			}
			return;
		}

		// M32.2 — Party decline packet (invitee declines a pending invite).
		PartyDeclineMessage partyDecline{};
		if (DecodePartyDecline(packetBytes, partyDecline))
		{
			ConnectedClient* sender = FindClient(datagram.endpoint);
			if (sender && m_partySystem.IsInitialized())
			{
				const PendingPartyInvite* invite = m_partySystem.GetPendingInvite(sender->clientId);
				const uint32_t inviterClientId   = invite ? invite->inviterClientId : 0;
				m_partySystem.DeclineInvite(sender->clientId);

				SendChatSystemNotice(*sender, "Party invite declined.");

				if (inviterClientId != 0)
				{
					for (ConnectedClient& c : m_clients)
					{
						if (c.clientId == inviterClientId)
						{
							const std::string senderLabel = "P" + std::to_string(sender->clientId);
							SendChatSystemNotice(c, "'" + senderLabel + "' declined your party invite.");
							break;
						}
					}
				}
				LOG_INFO(Net, "[ServerApp] Party invite declined via packet (client_id={})", sender->clientId);
			}
			return;
		}

		// M32.2 — Party leave packet (client explicitly leaves).
		PartyLeaveMessage partyLeave{};
		if (DecodePartyLeave(packetBytes, partyLeave))
		{
			ConnectedClient* sender = FindClient(datagram.endpoint);
			if (sender && m_partySystem.IsInitialized())
				HandleLeaveCommand(*sender);
			return;
		}

		LOG_WARN(Net, "[ServerApp] Dropped invalid packet from {}", UdpTransport::EndpointToString(datagram.endpoint));
	}

	void ServerApp::TickOnce()
	{
		++m_currentTick;
		// TA.3 — retraite avant Simulate : un Hello admis ce tick devient un client connecté
		// dont la position sera diffusée par MaybeSendSnapshots du même tick.
		DrainPendingHellos();
		// ETAPE2.7 — termine les Hello dont la lecture en base (hors du tick) est finie.
		DrainCompletedCharacterLoads();
		// ETAPE5.6 — ordres du master (prélèvement d'or, courrier à créditer).
		ProcessMasterInbox();
		// TA.3 — eviction des clients UDP zombies (1 Hz pour limiter le cout, suffisant
		// pour la propriete attendue : zombie disparait en max ~kUdpClientIdleTimeoutMs + 1 s).
		// ETAPE5.2 — reportée tant que dure la suspension qui suit un calage du tick.
		if (m_currentTick - m_lastEvictIdleClientsTick >= m_tickHz
			&& m_stallGuard.ChecksAllowed(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()))
		{
			EvictIdleUdpClients();
			m_lastEvictIdleClientsTick = m_currentTick;
		}
		Simulate();
		UpdateMapDiscovery();
		MaybeAutosaveCharacters();
		RefreshReplication();
		MaybeSendSnapshots();
		// ETAPE3.4 — renvois et accusés du canal fiable (après tout ce que le tick a émis).
		PumpReliableChannels();
		if ((m_currentTick % m_tickHz) == 0)
		{
			// ETAPE4.1 — mobs vivants / endormis (le sommeil doit dominer loin des joueurs).
			size_t asleepMobs = 0;
			engine::world::PopulationCounts mobsByClass{}; // POP.1
			for (const MobEntity& mob : m_mobs)
			{
				asleepMobs += mob.sleep.asleep ? 1u : 0u;
				++mobsByClass[engine::world::PopulationClassIndex(mob.populationClass)];
			}
			// FIX.6 — journal toutes les server.tick_stats_log_interval_sec (60 s par défaut)
			// au lieu de chaque seconde (368 lignes en 6 min), et aussi dès que le nombre de
			// clients ou l'état de délestage change. Les métriques (/metrics) restent à 1 Hz.
			if (m_tickStatsLogIntervalSec < 0)
				m_tickStatsLogIntervalSec = static_cast<int64_t>(std::clamp<int64_t>(
					m_config.GetInt("server.tick_stats_log_interval_sec", 60), 0, 86400));
			const uint64_t secondsSinceStart = m_currentTick / (std::max)(static_cast<uint64_t>(m_tickHz), uint64_t{1});
			const bool periodic = m_tickStatsLogIntervalSec > 0
				&& (secondsSinceStart % static_cast<uint64_t>(m_tickStatsLogIntervalSec)) == 0;
			const bool changed = m_clients.size() != m_lastTickStatsClients
				|| m_loadShedder.Active() != m_lastTickStatsShedding;
			m_lastTickStatsClients = m_clients.size();
			m_lastTickStatsShedding = m_loadShedder.Active();
			if (periodic || changed)
			{
				LOG_INFO(Core, "[ServerApp] Tick stats (tick={}, clients={}, mobs={}, mobs_endormis={}, charge_tick={:.0f}%, delestage={}, rx_packets={}, tx_packets={})",
					m_currentTick,
					m_clients.size(),
					m_mobs.size(),
					asleepMobs,
					m_loadShedder.SmoothedLoad() * 100.0,
					m_loadShedder.Active() ? "oui" : "non",
					m_transport.ReceivedPacketCount(),
					m_transport.SentPacketCount());
			}
			PublishGameplayMetrics(asleepMobs, mobsByClass);
		}
	}

	void ServerApp::Simulate()
	{
		// Monde « Royaumes » — régions actives (1 Hz) : les spawners d'une cellule sans
		// joueur proche ne sont pas évalués (leurs mobs restants sont retirés hors combat).
		RefreshRegionActivity();
		UpdateMobAi();
		UpdateSpawners();
		UpdateDynamicEvents();
		// M32.2 — Expire stale party invites once per tick.
		if (m_partySystem.IsInitialized())
			m_partySystem.ExpireInvites(m_currentTick);
		ProcessAuctionHouseTick();
		TickGathering();
		TickCrafting();
		// Combat SP3 — régénération de la ressource secondaire des joueurs,
		// avancement des casts en cours et tick des auras (DoT/HoT/expirations).
		RegenerateResources();
		TickActiveCasts();
		TickAuras();
		TickLava();
		TickBoats(); // v1.0.125 — bateaux et passagers
		// Anniversaires SP3 (2026-07-18) — buffs de gâteau (entretien 1 Hz) +
		// purge de minuit UTC des gâteaux expirés.
		TickCakeBuffs(NowMonotonicMs());
		// Combat SP4 — réplication des tables de menace modifiées (≤ 1/s par mob).
		PushThreatUpdates();
		LOG_TRACE(Core, "[ServerApp] Simulate tick {}", m_currentTick);
	}
}
