// ServerApp — persistance : instantanés de personnage, découverte de la carte, sauvegardes automatiques et transactions à deux personnages.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	void ServerApp::RecordCharacterSnapshot(const ConnectedClient& client, std::string_view reason)
	{
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr || client.persistenceCharacterKey == 0u)
		{
			return;
		}
		const uint64_t characterKey = client.persistenceCharacterKey;
		auto payload = std::make_shared<std::string>(SerializeCharacterState(BuildPersistedCharacterState(client)));
		const std::string reasonText(reason.substr(0, 32));
		const uint64_t generation = client.ownerGeneration;
		auto job = [this, characterKey, payload, reasonText, generation]() {
			auto guard = m_characterDbPool->Acquire();
			MYSQL* mysql = guard.get();
			auto* cache = guard.cache();
			if (mysql == nullptr || cache == nullptr)
			{
				LOG_WARN(Net, "[ServerApp] Character snapshot skipped: DB unavailable (character_key={})", characterKey);
				return;
			}
			// Seul le propriétaire enregistre un instantané : celui d'un shard qui ne
			// l'est plus (sauvegarde de départ refusée) pourrait restaurer un état non sauvé.
			auto* own = cache->Acquire(mysql,
				"SELECT IFNULL(owner_shard_id, 0), owner_generation FROM characters WHERE id = ? AND deleted_at IS NULL");
			if (own == nullptr || !own->Bind(0, characterKey) || !own->Execute() || !own->FetchRow()
				|| !db::IsWriteAllowed(db::CheckCharacterOwnership(static_cast<uint32_t>(own->GetUInt64(0)),
					own->GetUInt64(1), m_shardId, generation)))
			{
				LOG_WARN(Net, "[ServerApp] Character snapshot skipped: shard is not the owner (character_key={})", characterKey);
				return;
			}
			while (own->FetchRow()) {}
			auto* ins = cache->Acquire(mysql,
				"INSERT INTO character_snapshots (character_id, reason, shard_id, payload) VALUES (?, ?, ?, ?)");
			if (ins == nullptr || !ins->Bind(0, characterKey) || !ins->Bind(1, std::string_view(reasonText))
				|| !ins->Bind(2, m_shardId) || !ins->Bind(3, std::string_view(*payload)) || !ins->Execute())
			{
				LOG_WARN(Net, "[ServerApp] Character snapshot insert FAILED (character_key={})", characterKey);
				return;
			}
			// Garde les 20 plus récents de ce personnage.
			// (sauf ceux visés par une demande de restauration en attente).
			auto* prune = cache->Acquire(mysql,
				"DELETE s FROM character_snapshots s "
				"LEFT JOIN (SELECT id FROM character_snapshots WHERE character_id = ? ORDER BY id DESC LIMIT 20) k "
				"ON k.id = s.id "
				"LEFT JOIN character_restore_requests r ON r.snapshot_id = s.id AND r.applied_at IS NULL "
				"WHERE s.character_id = ? AND k.id IS NULL AND r.snapshot_id IS NULL");
			if (prune == nullptr || !prune->Bind(0, characterKey) || !prune->Bind(1, characterKey) || !prune->Execute())
			{
				LOG_WARN(Net, "[ServerApp] Character snapshot prune FAILED (character_key={})", characterKey);
			}
			LOG_INFO(Net, "[ServerApp] Character snapshot recorded (character_key={}, reason={})", characterKey, reasonText);
		};
		// Même thread que les sauvegardes de ce personnage : l'instantané part après
		// la sauvegarde de départ. Sans file (ou file pleine) : tout de suite.
		if (m_characterSaveQueue == nullptr || !m_characterSaveQueue->IsRunning()
			|| m_characterSaveQueue->EnqueueOrdered(characterKey, job) == CharacterSaveQueue::EnqueueResult::Rejected)
		{
			job();
		}
#else
		(void)client;
		(void)reason;
#endif
	}

	void ServerApp::UpdateMapDiscovery()
	{
		// Positions globales seulement dans le monde en cellules (en zone historique,
		// la position est locale à la zone et ne correspond à aucune case de la carte).
		if (!m_cellWorld)
		{
			return;
		}
		for (ConnectedClient& client : m_clients)
		{
			// ETAPE4.2 — chaque joueur à son tour (4 Hz par défaut) : le joueur avance
			// d'au plus 3 m entre deux passages, bien moins qu'une case de la carte.
			if (!tick_slicing::IsSliceTurn(client.clientId, m_currentTick, m_mapDiscoveryPeriodTicks))
			{
				continue;
			}
			// Lore v1.0.114 — cimetières et lieux secrets (même tranche, les deux cartes).
			UpdateDiscoveryProgress(client);
			// Zone non cellulaire (instance, zone historique) : position locale, pas de carte.
			// Seconde carte : la carte à découvrir ne couvre que le continent (les cases
			// sont indexées par cellule des Royaumes) — rien n'est révélé aux Enfers.
			if (engine::world::cells::MapIdOfZone(client.zoneId) != engine::world::cells::kCellWorldMapId
				|| !std::isfinite(client.positionMetersX) || !std::isfinite(client.positionMetersZ))
			{
				continue;
			}
			(void)client.mapDiscovery.RevealAround(client.positionMetersX, client.positionMetersZ);
		}
	}

	void ServerApp::UpdateDiscoveryProgress(ConnectedClient& client)
	{
		// Vivant seulement (un mort ne visite rien ; sa réapparition au cimetière compte
		// dans HandleRespawnRequest), position globale d'une cellule (pas d'instance).
		if (!client.discovery.Loaded() || (client.stateFlags & kEntityStateDead) != 0u
			|| !engine::world::cells::CellMapOfZoneId(client.zoneId).has_value()
			|| !std::isfinite(client.positionMetersX) || !std::isfinite(client.positionMetersZ))
		{
			return;
		}
		DiscoveryEvents events;
		client.discovery.ObservePosition(m_discoveryCatalog, ClientCellMap(client),
			client.positionMetersX, client.positionMetersZ, events);
		if (!events.Empty())
		{
			SendDiscoveryEvents(client, events);
		}
	}

	void ServerApp::SendDiscoveryEvents(const ConnectedClient& client, const DiscoveryEvents& events)
	{
		const uint16_t visited = static_cast<uint16_t>(std::min<uint32_t>(client.discovery.VisitedCount(), 0xFFFFu));
		const uint16_t total = static_cast<uint16_t>(std::min<size_t>(m_discoveryCatalog.GraveyardCount(), 0xFFFFu));
		for (size_t i = 0; i < events.graveyards.size(); ++i)
		{
			// Compteur propre à chaque visite (plusieurs cimetières dans une même observation).
			const size_t after = events.graveyards.size() - 1u - i;
			DiscoveryEventMessage message{};
			message.kind = DiscoveryEventKind::GraveyardVisited;
			message.graveyardsVisited = static_cast<uint16_t>(visited >= after ? visited - after : 0u);
			message.graveyardsTotal = total;
			message.id = events.graveyards[i];
			(void)SendDatagram(client.endpoint, EncodeDiscoveryEvent(message));
		}
		for (const uint32_t index : events.secretPlaces)
		{
			const SecretPlaceSite& place = m_discoveryCatalog.SecretPlace(index);
			DiscoveryEventMessage message{};
			message.kind = DiscoveryEventKind::PlaceDiscovered;
			message.graveyardsVisited = visited;
			message.graveyardsTotal = total;
			message.id = place.id;
			message.title = place.name;
			message.place = { place.id, place.name, place.kind, place.x, place.z, static_cast<uint8_t>(place.map) };
			(void)SendDatagram(client.endpoint, EncodeDiscoveryEvent(message));
			LOG_INFO(Net, "[ServerApp] Lieu secret decouvert (character_key={}, lieu={})", client.persistenceCharacterKey, place.id);
		}
		if (events.exploitUnlocked)
		{
			DiscoveryEventMessage message{};
			message.kind = DiscoveryEventKind::ExploitUnlocked;
			message.graveyardsVisited = visited;
			message.graveyardsTotal = total;
			message.id = std::string(kPilgrimExploitCode);
			message.title = std::string(kPilgrimExploitTitle);
			(void)SendDatagram(client.endpoint, EncodeDiscoveryEvent(message));
			LOG_INFO(Net, "[ServerApp] Exploit {} debloque (character_key={}, cimetieres={}/{})",
				kPilgrimExploitCode, client.persistenceCharacterKey, visited, total);
		}
	}

	void ServerApp::SendDiscoveryState(const ConnectedClient& client)
	{
		std::vector<DiscoveredPlaceWire> places;
		for (const uint32_t index : client.discovery.DiscoveredPlaceIndices(m_discoveryCatalog))
		{
			const SecretPlaceSite& place = m_discoveryCatalog.SecretPlace(index);
			places.push_back({ place.id, place.name, place.kind, place.x, place.z, static_cast<uint8_t>(place.map) });
		}
		const uint16_t visited = static_cast<uint16_t>(std::min<uint32_t>(client.discovery.VisitedCount(), 0xFFFFu));
		const uint16_t total = static_cast<uint16_t>(std::min<size_t>(m_discoveryCatalog.GraveyardCount(), 0xFFFFu));
		for (const std::vector<std::byte>& packet : EncodeDiscoveryStatePackets(visited, total, places))
			(void)SendDatagram(client.endpoint, packet);
	}

	void ServerApp::MaybeAutosaveCharacters()
	{
		// ETAPE2.3 — sauvegardes étalées : à chaque tick, seuls les personnages dont
		// c'est le tour (id % période == tick % période) sont sauvés. Chaque personnage
		// est toujours sauvé une fois par période, sans pic toutes les 30 s.
		if (m_clients.empty() || m_characterAutosaveIntervalTicks == 0)
		{
			return;
		}

		size_t saved = 0;
		for (const ConnectedClient& client : m_clients)
		{
			if (IsCharacterAutosaveDue(client.persistenceCharacterKey, m_currentTick, m_characterAutosaveIntervalTicks))
			{
				SaveConnectedClient(client, "autosave");
				++saved;
			}
		}

		// Bilan une fois par période (pas à chaque tick).
		if (m_currentTick >= m_nextCharacterAutosaveTick)
		{
			m_nextCharacterAutosaveTick = m_currentTick + m_characterAutosaveIntervalTicks;
			LOG_INFO(Net, "[ServerApp] Character autosave (staggered) : clients={}, period_ticks={}, save_queue_pending={}",
				m_clients.size(),
				m_characterAutosaveIntervalTicks,
				m_characterSaveQueue != nullptr ? m_characterSaveQueue->PendingCount() : 0u);
		}
		if (saved > 0u)
		{
			LOG_DEBUG(Net, "[ServerApp] Character autosave tick={} saved={}", m_currentTick, saved);
		}
	}

	PersistedCharacterState ServerApp::BuildPersistedCharacterState(const ConnectedClient& client) const
	{
		// ETAPE2.4 — instantané commun aux sauvegardes simples et à deux personnages.
		PersistedCharacterState state{};
		state.characterKey = client.persistenceCharacterKey;
		state.zoneId = client.zoneId;
		state.positionMetersX = client.positionMetersX;
		state.positionMetersY = client.positionMetersY;
		state.positionMetersZ = client.positionMetersZ;
		// v1.0.125 — passager d'un bateau : sauvegardé au ponton de l'escale (à quai : celle du
		// quai ; en mer : celle d'arrivée), jamais au milieu de la mer.
		if (client.boatIndex >= 0 && static_cast<size_t>(client.boatIndex) < m_boatPoses.size())
		{
			const BoatPose& p = m_boatPoses[static_cast<size_t>(client.boatIndex)];
			const SeaLine& line = m_boatFleet.LineOf(static_cast<size_t>(client.boatIndex));
			const SeaLineStop& st = line.stops[(std::min)(p.stop, line.stops.size() - 1)];
			state.positionMetersX = static_cast<float>(st.x);
			state.positionMetersY = static_cast<float>(st.y);
			state.positionMetersZ = static_cast<float>(st.z);
			state.zoneId = engine::world::cells::CellZoneId(engine::world::cells::CellFromPosition(st.x, st.z), line.map);
		}
		state.experiencePoints = client.experiencePoints;
		state.level = client.level;
		state.gold = client.gold;
		state.honor = client.honor;
		state.badges = client.badges;
		state.premiumCurrency = client.premiumCurrency;
		state.stats = client.stats;
		state.inventory = client.inventory;
		state.equipment = client.equipment; // Chantier 2 SP-A
		state.questStates = client.questStates;
		state.chatIgnoredDisplayNames = client.chatIgnoredDisplayNames;
		state.chatModeratorRole = client.chatModeratorRole;
		state.mailboxGold = 0;
		state.mailboxItems.clear();
		state.mailboxClaimedIds = client.mailboxClaimedIds; // ETAPE2.1
		/// M36.2 — persist profession skill progress.
		state.professions = client.professions;
		/// Grimoire — persist action bar layout.
		state.actionBarLayout = client.actionBarLayout;
		/// Roadmap-3 — persist la ceinture (4 slots d'objets actifs).
		state.beltLayout = client.beltLayout;
		/// Anniversaires SP3 — persist les expirations des gâteaux possédés.
		state.cakeExpiresAtMsUtcByItemId = client.cakeExpiresAtMsUtcByItemId;
		/// SP-B — persist known class skills.
		state.knownSkillIds = client.knownSkillIds;
		/// ETAPE2.5 — carte découverte pendant la session (fusion OU en base).
		state.mapDiscoveryCells = client.mapDiscovery.SessionCells();
		// Lore v1.0.114 — nouveautés d'exploration de la session (vides si suivi désactivé).
		state.graveyardVisits = client.discovery.SessionGraveyardVisits();
		state.secretDiscoveries = client.discovery.SessionSecretDiscoveries();
		state.grantPilgrimExploit = client.discovery.ExploitGrantPending();
		state.restoreSnapshotId = client.restoreSnapshotId; // ETAPE2.6
		return state;
	}

	void ServerApp::SaveConnectedClient(const ConnectedClient& client, std::string_view reason)
	{
		if (client.persistenceCharacterKey == 0)
		{
			LOG_WARN(Net, "[ServerApp] Character save skipped: missing persistence key (client_id={}, reason={})",
				client.clientId,
				reason);
			return;
		}

		PersistedCharacterState state = BuildPersistedCharacterState(client);
		// ETAPE2.2 — avec la file de sauvegarde, le tick dépose l'instantané et
		// repart : l'écriture SQL se fait sur un thread de la file (même thread pour
		// un personnage donné, l'ordre est gardé ; une sauvegarde en attente est
		// remplacée par la plus récente). File pleine : écriture immédiate ici.
		if (m_characterSaveQueue != nullptr && m_characterSaveQueue->IsRunning())
		{
			auto snapshot = std::make_shared<PersistedCharacterState>(std::move(state));
			const float yaw = client.yawRadians;
			const uint64_t generation = client.ownerGeneration;
			const uint32_t clientId = client.clientId;
			std::string reasonCopy(reason);
			auto job = [this, snapshot, yaw, generation, clientId, reasonCopy]() {
				PersistCharacterState(*snapshot, yaw, generation, clientId, reasonCopy);
			};
			// ETAPE5.5 — un état restauré (ETAPE2.6) ne doit jamais être fusionné avec une
			// sauvegarde plus récente qui ne porte plus restoreSnapshotId : il écrit le niveau
			// restauré et marque la demande appliquée. Dépôt ordonné, jamais fusionné.
			const auto result = snapshot->restoreSnapshotId != 0u
				? m_characterSaveQueue->EnqueueOrdered(client.persistenceCharacterKey, std::move(job))
				: m_characterSaveQueue->EnqueueSave(client.persistenceCharacterKey, std::move(job));
			if (result != CharacterSaveQueue::EnqueueResult::Rejected)
			{
				return;
			}
			LOG_WARN(Net, "[ServerApp] Character save queue full (pending={}) : écriture immédiate (client_id={}, reason={})",
				m_characterSaveQueue->PendingCount(), client.clientId, reason);
			// Un thread peut être en train d'écrire un instantané PLUS ANCIEN du même
			// personnage : on le laisse finir, sinon il passerait après celui-ci.
			// ETAPE5.5 — s'il n'a pas fini en 2 s, les deux écritures se chevauchent : la
			// référence différentielle est invalidée (écriture complète, et le jeton de
			// version empêche l'écriture ancienne de réinstaller sa référence).
			if (!m_characterSaveQueue->Flush(client.persistenceCharacterKey))
				m_savedBlocks.Invalidate(client.persistenceCharacterKey);
			PersistCharacterState(*snapshot, yaw, generation, clientId, reason);
			return;
		}
		PersistCharacterState(state, client.yawRadians, client.ownerGeneration, client.clientId, reason);
	}

	ServerApp::CharacterDbSaveResult ServerApp::SaveCharacterPairToDb(
		const PersistedCharacterState& first, float firstYawRadians, uint64_t firstOwnerGeneration,
		const PersistedCharacterState& second, float secondYawRadians, uint64_t secondOwnerGeneration,
		std::string_view kind, std::string_view operationId)
	{
		// ETAPE5.5 — écriture complète par un autre chemin : les empreintes de ces deux
		// personnages ne sont plus la référence (prochaine sauvegarde simple complète).
		m_savedBlocks.Invalidate(first.characterKey);
		m_savedBlocks.Invalidate(second.characterKey);
		// Rejouée jusqu'à 3 fois avec le MÊME identifiant : si un essai a en fait été
		// validé (réponse du COMMIT perdue), le suivant reçoit AlreadyApplied et ne
		// réapplique rien.
		CharacterDbSaveResult result = CharacterDbSaveResult::Failed;
		for (int attempt = 1; attempt <= 3; ++attempt)
		{
			bool connected = false;
			result = SaveCharacterPairToDbOnce(first, firstYawRadians, firstOwnerGeneration,
				second, secondYawRadians, secondOwnerGeneration, kind, operationId, connected);
			if (result != CharacterDbSaveResult::Failed || !connected)
			{
				return result;
			}
			if (attempt < 3)
			{
				LOG_WARN(Net, "[ServerApp] SaveCharacterPairToDb: retry {} (operation={})", attempt, operationId);
				std::this_thread::sleep_for(std::chrono::milliseconds(20 * attempt));
			}
		}
		return result;
	}

	ServerApp::CharacterDbSaveResult ServerApp::SaveCharacterPairToDbOnce(
		const PersistedCharacterState& first, float firstYawRadians, uint64_t firstOwnerGeneration,
		const PersistedCharacterState& second, float secondYawRadians, uint64_t secondOwnerGeneration,
		std::string_view kind, std::string_view operationId, bool& outConnected)
	{
		outConnected = false;
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr || first.characterKey == 0u || second.characterKey == 0u)
		{
			return CharacterDbSaveResult::SkippedNoDb;
		}
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
		{
			return CharacterDbSaveResult::Failed;
		}
		outConnected = true;
		(void)mysql_query(mysql, "SET TRANSACTION ISOLATION LEVEL READ COMMITTED"); // cf. SaveCharacterToDbOnce
		if (mysql_query(mysql, "START TRANSACTION") != 0)
		{
			LOG_WARN(Net, "[ServerApp] SaveCharacterPairToDb: START TRANSACTION failed (operation={})", operationId);
			return CharacterDbSaveResult::Failed;
		}
		// 1. L'opération d'abord : INSERT IGNORE + 0 ligne insérée = identifiant déjà
		// présent, l'opération a déjà été écrite → on n'applique rien une deuxième fois.
		auto* op = cache->Acquire(mysql,
			"INSERT IGNORE INTO character_operations (operation_id, kind, character_a, character_b, shard_id) "
			"VALUES (?, ?, ?, ?, ?)");
		if (op == nullptr
			|| !op->Bind(0, operationId)
			|| !op->Bind(1, kind)
			|| !op->Bind(2, first.characterKey)
			|| !op->Bind(3, second.characterKey)
			|| !op->Bind(4, m_shardId)
			|| !op->Execute())
		{
			mysql_query(mysql, "ROLLBACK");
			LOG_WARN(Net, "[ServerApp] SaveCharacterPairToDb: operation insert failed (operation={})", operationId);
			return CharacterDbSaveResult::Failed;
		}
		if (op->AffectedRows() == 0u)
		{
			mysql_query(mysql, "ROLLBACK");
			LOG_WARN(Net, "[ServerApp] SaveCharacterPairToDb: operation already applied (operation={})", operationId);
			return CharacterDbSaveResult::AlreadyApplied;
		}
		// 2. Les deux personnages, toujours dans l'ordre croissant des identifiants :
		// deux transactions concurrentes verrouillent leurs lignes dans le même ordre
		// (pas d'interblocage). Chacun passe le contrôle de propriété de 1.2.
		const bool firstIsLower = first.characterKey <= second.characterKey;
		const PersistedCharacterState& lowState = firstIsLower ? first : second;
		const PersistedCharacterState& highState = firstIsLower ? second : first;
		const float lowYaw = firstIsLower ? firstYawRadians : secondYawRadians;
		const float highYaw = firstIsLower ? secondYawRadians : firstYawRadians;
		const uint64_t lowGen = firstIsLower ? firstOwnerGeneration : secondOwnerGeneration;
		const uint64_t highGen = firstIsLower ? secondOwnerGeneration : firstOwnerGeneration;
		CharacterDbSaveResult result = WriteCharacterStateInTx(mysql, cache, lowState, lowYaw, lowGen);
		if (result == CharacterDbSaveResult::Saved)
		{
			result = WriteCharacterStateInTx(mysql, cache, highState, highYaw, highGen);
		}
		if (result != CharacterDbSaveResult::Saved)
		{
			mysql_query(mysql, "ROLLBACK");
			LOG_WARN(Net, "[ServerApp] SaveCharacterPairToDb: rollback (operation={}, character_a={}, character_b={}, refused_not_owner={})",
				operationId, first.characterKey, second.characterKey,
				result == CharacterDbSaveResult::RefusedNotOwner ? "true" : "false");
			return result;
		}
		if (mysql_query(mysql, "COMMIT") != 0)
		{
			mysql_query(mysql, "ROLLBACK");
			LOG_WARN(Net, "[ServerApp] SaveCharacterPairToDb: COMMIT failed (operation={})", operationId);
			return CharacterDbSaveResult::Failed;
		}
		return CharacterDbSaveResult::Saved;
#else
		(void)first; (void)firstYawRadians; (void)firstOwnerGeneration;
		(void)second; (void)secondYawRadians; (void)secondOwnerGeneration;
		(void)kind; (void)operationId;
		return CharacterDbSaveResult::SkippedNoDb;
#endif
	}

	void ServerApp::SaveCharacterPair(const ConnectedClient& first, const ConnectedClient& second, std::string_view kind)
	{
		if (first.persistenceCharacterKey == 0u || second.persistenceCharacterKey == 0u
			|| first.persistenceCharacterKey == second.persistenceCharacterKey)
		{
			SaveConnectedClient(first, kind);
			SaveConnectedClient(second, kind);
			return;
		}
		// Les sauvegardes déjà en file pour ces deux personnages sont plus anciennes :
		// on les laisse finir pour qu'elles ne passent pas APRÈS l'opération.
		// Si elles ne finissent pas à temps (base très lente), pas de transaction :
		// une sauvegarde en file de chacun, dans l'ordre (moins sûr, mais jamais un
		// état ancien écrit après l'opération).
		if (m_characterSaveQueue != nullptr
			&& (!m_characterSaveQueue->Flush(first.persistenceCharacterKey)
				|| !m_characterSaveQueue->Flush(second.persistenceCharacterKey)))
		{
			LOG_ERROR(Net, "[ServerApp] Character pair save: pending saves not finished, queued separately (character_a={}, character_b={}, kind={})",
				first.persistenceCharacterKey, second.persistenceCharacterKey, kind);
			SaveConnectedClient(first, kind);
			SaveConnectedClient(second, kind);
			return;
		}
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
		const std::string operationId = MakeCharacterOperationId(kind, m_shardId, nowMs, ++m_characterOperationCounter);
		PersistedCharacterState firstState = BuildPersistedCharacterState(first);
		PersistedCharacterState secondState = BuildPersistedCharacterState(second);
		const CharacterDbSaveResult result = SaveCharacterPairToDb(
			firstState, first.yawRadians, first.ownerGeneration,
			secondState, second.yawRadians, second.ownerGeneration,
			kind, operationId);
		switch (result)
		{
		case CharacterDbSaveResult::Saved:
			LOG_INFO(Net, "[ServerApp] Character pair saved (operation={}, character_a={}, character_b={})",
				operationId, first.persistenceCharacterKey, second.persistenceCharacterKey);
			return;
		case CharacterDbSaveResult::AlreadyApplied:
			return;
		case CharacterDbSaveResult::SkippedNoDb:
			// Sans base : deux fichiers .ini, comme avant (pas de transaction possible).
			SaveConnectedClient(first, kind);
			SaveConnectedClient(second, kind);
			return;
		case CharacterDbSaveResult::RefusedNotOwner:
		case CharacterDbSaveResult::Failed:
			// Rien n'a été écrit (ROLLBACK). Les sauvegardes suivantes réécriront
			// l'état complet de chacun ; on signale l'incident pour enquête.
			LOG_ERROR(Net, "[ServerApp] Character pair save FAILED (operation={}, character_a={}, character_b={}, kind={})",
				operationId, first.persistenceCharacterKey, second.persistenceCharacterKey, kind);
			return;
		}
	}

	void ServerApp::PersistCharacterState(const PersistedCharacterState& state, float yawRadians,
		uint64_t ownerGeneration, uint32_t clientId, std::string_view reason)
	{
		// ETAPE2.1 — avec une base, elle seule reçoit l'état (plus de fichier .ini).
		// ETAPE1.2 — un shard qui n'est plus propriétaire du personnage n'écrit rien.
		// Sans base (dev sans MySQL, shard Windows), le fichier .ini reste le stockage.
		// ETAPE2.2 — appelée depuis le tick ou depuis un thread de la file : n'utilise
		// que le pool (thread-safe), m_shardId et le stockage fichier (sans base, donc
		// jamais depuis la file).
		const CharacterDbSaveResult dbResult = SaveCharacterToDb(state, yawRadians, ownerGeneration);
		switch (dbResult)
		{
		case CharacterDbSaveResult::Saved:
		case CharacterDbSaveResult::AlreadyApplied: // propre aux opérations à deux personnages
			break;
		case CharacterDbSaveResult::RefusedNotOwner:
			LOG_WARN(Net, "[ServerApp] Character save REFUSED: shard is not the owner (client_id={}, character_key={}, shard_id={}, owner_generation={}, reason={})",
				clientId, state.characterKey, m_shardId, ownerGeneration, reason);
			return;
		case CharacterDbSaveResult::Failed:
			LOG_ERROR(Net, "[ServerApp] Character DB save FAILED (client_id={}, character_key={}, reason={})",
				clientId, state.characterKey, reason);
			return;
		case CharacterDbSaveResult::SkippedNoDb:
			if (!m_characterPersistence.SaveCharacter(state))
			{
				LOG_WARN(Net, "[ServerApp] Character save FAILED (client_id={}, character_key={}, reason={})",
					clientId, state.characterKey, reason);
				return;
			}
			break;
		}

		LOG_INFO(Net, "[ServerApp] Character save OK (client_id={}, character_key={}, reason={})",
			clientId, state.characterKey, reason);
	}
}
