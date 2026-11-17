// ServerApp — monde : intérêt et instantanés, transitions de zone, portails, lave, changement de carte, monde en cellules.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	void ServerApp::MaybeSendSnapshots()
	{
		if (m_clients.empty())
		{
			return;
		}

		m_snapshotAccumulator += m_snapshotHz;
		if (m_snapshotAccumulator < m_tickHz)
		{
			return;
		}

		m_snapshotAccumulator -= m_tickHz;
		for (ConnectedClient& client : m_clients)
		{
			(void)SendSnapshot(client);
		}
	}

	void ServerApp::UpdateClientInterest(ConnectedClient& client)
	{
		// Monde « Royaumes » — la région (cellule) suit la position, sans changer de grille.
		UpdateClientRegion(client);
		CellGrid* zoneGrid = GetOrCreateZoneGrid(client.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Interest update FAILED: zone grid unavailable (zone_id={})", client.zoneId);
			return;
		}

		CellCoord newCell{};
		if (!zoneGrid->UpsertEntity(client.entityId, client.positionMetersX, client.positionMetersZ, newCell))
		{
			LOG_WARN(Net, "[ServerApp] Interest update skipped (client_id={}, pos=({:.2f}, {:.2f}))",
				client.clientId,
				client.positionMetersX,
				client.positionMetersZ);
			return;
		}

		if (client.hasCell && client.currentCell == newCell)
		{
			zoneGrid->GatherEntityIds(client.interestCells, client.interestEntityIds);
			return;
		}

		const std::vector<CellCoord> previousCells = client.interestCells;
		client.currentCell = newCell;
		client.hasCell = true;
		zoneGrid->BuildInterestSet(client.currentCell, client.interestCells);
		zoneGrid->GatherEntityIds(client.interestCells, client.interestEntityIds);

		InterestDiff diff{};
		ComputeInterestDiff(previousCells, client.interestCells, diff);
		LOG_INFO(Net,
			"[ServerApp] Interest set updated (client_id={}, zone_id={}, cell={}, {}, entering={}, leaving={}, entities={})",
			client.clientId,
			client.zoneId,
			client.currentCell.x,
			client.currentCell.z,
			diff.enteringCells.size(),
			diff.leavingCells.size(),
			client.interestEntityIds.size());
	}

	void ServerApp::MaybeApplyZoneTransition(ConnectedClient& client)
	{
		ZoneChangeMessage zoneChange{};
		if (!m_zoneTransitionMap.ResolveTransition(client.zoneId, client.positionMetersX, client.positionMetersZ, zoneChange))
		{
			return;
		}

		if (client.hasCell)
		{
			CellGrid* oldZoneGrid = GetOrCreateZoneGrid(client.zoneId);
			if (oldZoneGrid != nullptr)
			{
				(void)oldZoneGrid->RemoveEntity(client.entityId);
			}
		}

		client.zoneId = zoneChange.zoneId;
		// Présence unifiée : maintient la zone à jour pour le snapshot master / amis
		// (même clé de repli qu'au login : account_id, sinon character_id).
		m_shardPresence.UpdateZone(client.accountId != 0 ? client.accountId
			: static_cast<uint64_t>(client.persistenceCharacterKey), client.zoneId);
		client.positionMetersX = zoneChange.spawnPositionX;
		client.positionMetersY = zoneChange.spawnPositionY;
		client.positionMetersZ = zoneChange.spawnPositionZ;
		client.velocityMetersPerSecondX = 0.0f;
		client.velocityMetersPerSecondY = 0.0f;
		client.velocityMetersPerSecondZ = 0.0f;
		client.hasCell = false;
		client.currentCell = {};
		client.interestCells.clear();
		client.interestEntityIds.clear();
		client.replicatedEntityIds.clear();
		LOG_INFO(Net,
			"[ServerApp] Zone transition applied (client_id={}, zone_id={}, spawn=({:.2f}, {:.2f}, {:.2f}))",
			client.clientId,
			client.zoneId,
			client.positionMetersX,
			client.positionMetersY,
			client.positionMetersZ);
		SaveConnectedClient(client, "zone_transition");
		(void)SendZoneChange(client, zoneChange);
		// ETAPE3.3 — la position officielle repart du point d'arrivée dans une nouvelle
		// époque : les commandes encore en route depuis l'ancienne zone sont ignorées.
		SendForcePosition(client, kForcePositionReasonTeleport);
		SendDynamicEventBootstrap(client);
		ApplyQuestEvent(client, QuestStepType::Enter, std::string("zone:") + std::to_string(client.zoneId), 1, "zone_enter");
	}

	void ServerApp::SendMapZoneChange(ConnectedClient& client)
	{
		ZoneChangeMessage zoneChange{};
		zoneChange.zoneId = client.zoneId;
		zoneChange.spawnPositionX = client.positionMetersX;
		zoneChange.spawnPositionY = client.positionMetersY;
		zoneChange.spawnPositionZ = client.positionMetersZ;
		(void)SendZoneChange(client, zoneChange);
	}

	void ServerApp::MaybeApplyPortal(ConnectedClient& client)
	{
		if (!m_cellWorld || m_portalNetwork.Count() == 0u || !engine::world::cells::IsCellZoneId(client.zoneId))
		{
			return;
		}
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		// Un joueur mort ne passe pas : son armement est conservé, seul le passage est bloqué.
		const bool alive = client.stats.currentHealth > 0u && (client.stateFlags & kEntityStateDead) == 0u;
		const std::optional<PortalDestination> destination = m_portalNetwork.Update(client.portalTravel,
			ClientCellMap(client), client.positionMetersX, client.positionMetersZ, alive, nowMs);
		if (!destination)
		{
			return;
		}
		LOG_INFO(Net, "[ServerApp] Portail franchi (client_id={}, race='{}', {}:{} -> {}:{})",
			client.clientId, client.raceId, engine::world::cells::WorldIdOf(ClientCellMap(client)), destination->fromId,
			engine::world::cells::WorldIdOf(destination->map), destination->toId);
		if (!TransferToMap(client, destination->map, static_cast<float>(destination->x), static_cast<float>(destination->y),
				static_cast<float>(destination->z), "portal"))
		{
			LOG_WARN(Net, "[ServerApp] Portail '{}' : arrivee '{}' hors des cellules generees (passage annule, client_id={})",
				destination->fromId, destination->toId, client.clientId);
		}
	}

	void ServerApp::InitLavaHazard()
	{
		namespace cells = engine::world::cells;
		m_lavaHazard.Clear();
		if (!m_cellWorld)
			return;
		for (const cells::CellCoord& cell : m_worldStarts.GeneratedCells(cells::CellMap::Enfers))
		{
			const std::string path = "zones/" + cells::CellDirName(cell, cells::CellMap::Enfers) + "/lava_mask.lvmk";
			const std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_config, path);
			if (bytes.empty())
				continue; // cellule sans lave
			engine::world::LavaMask mask;
			std::string err;
			if (!engine::world::LoadLavaMask(bytes, mask, err))
			{
				LOG_WARN(Net, "[ServerApp] Masque de lave ignore ({}) : {}", path, err);
				continue;
			}
			m_lavaHazard.AddMask(std::move(mask));
		}
		LOG_INFO(Net, "[ServerApp] Lave des Enfers : {} masque(s), mort apres {} s", m_lavaHazard.Count(),
			LavaHazard::kDeathSeconds);
	}

	void ServerApp::TickLava()
	{
		if (m_lavaHazard.Count() == 0u)
			return;
		const float dt = 1.0f / static_cast<float>((std::max)(static_cast<unsigned>(m_tickHz), 1u));
		for (ConnectedClient& client : m_clients)
		{
			const bool onEnfers = engine::world::cells::IsCellZoneId(client.zoneId)
				&& ClientCellMap(client) == engine::world::cells::CellMap::Enfers;
			// MJ invincible (/invincible) : jamais brûlé, comme pour les autres dégâts.
			const bool alive = client.stats.currentHealth > 0u && (client.stateFlags & kEntityStateDead) == 0u
				&& !client.gm.invincible;
			const LavaEvent ev = m_lavaHazard.Update(client.lavaExposure, onEnfers, alive,
				client.positionMetersX, client.positionMetersY, client.positionMetersZ, dt);
			if (ev == LavaEvent::Entered)
			{
				SendChatSystemNotice(client, "La lave vous brûle : sortez-en, elle tue en 3 secondes !");
				continue;
			}
			if (ev != LavaEvent::Killed)
				continue;
			const uint32_t lost = client.stats.currentHealth;
			client.stats.currentHealth = 0;
			client.stateFlags |= kEntityStateDead;
			LOG_INFO(Net, "[ServerApp] Player died in lava (entity_id={}, x={:.1f}, z={:.1f})",
				client.entityId, client.positionMetersX, client.positionMetersZ);
			PurgeThreatForEntity(client.entityId);
			if (!client.auras.empty())
			{
				client.auras.clear();
				BroadcastAuraUpdate(client.entityId, client.auras);
			}
			// Événement de combat sans attaquant (0) : le client affiche la mort comme
			// pour tout autre dégât (même chemin que /kill).
			CombatEventMessage event{};
			event.attackerEntityId = 0;
			event.targetEntityId = client.entityId;
			event.damage = lost;
			event.targetCurrentHealth = 0;
			event.targetMaxHealth = client.stats.maxHealth;
			event.targetStateFlags = client.stateFlags;
			BroadcastCombatEvent(event);
			SaveConnectedClient(client, "lava_death");
			SendChatSystemNotice(client, "Vous avez péri dans la lave.");
		}
	}

	bool ServerApp::TransferToMap(ConnectedClient& client, engine::world::cells::CellMap map, float x, float y, float z,
		const char* reason)
	{
		namespace cells = engine::world::cells;
		if (!m_cellWorld || !std::isfinite(x) || !std::isfinite(z) || !IsInsideGeneratedWorld(x, z, map))
		{
			return false;
		}
		// Ancienne carte : l'entité quitte sa grille d'intérêt (une grille par carte).
		if (client.hasCell)
		{
			if (CellGrid* oldGrid = GetOrCreateZoneGrid(client.zoneId); oldGrid != nullptr)
			{
				(void)oldGrid->RemoveEntity(client.entityId);
			}
		}
		const uint32_t previousZoneId = client.zoneId;
		client.zoneId = cells::CellZoneId(cells::CellFromPosition(x, z), map);
		m_shardPresence.UpdateZone(client.accountId != 0 ? client.accountId
			: static_cast<uint64_t>(client.persistenceCharacterKey), client.zoneId);
		client.positionMetersX = x;
		client.positionMetersY = std::isfinite(y) ? y : client.positionMetersY;
		client.positionMetersZ = z;
		client.velocityMetersPerSecondX = 0.0f;
		client.velocityMetersPerSecondY = 0.0f;
		client.velocityMetersPerSecondZ = 0.0f;
		client.hasCell = false;
		client.currentCell = {};
		client.interestCells.clear();
		client.interestEntityIds.clear();
		client.replicatedEntityIds.clear();
		// Repli de réapparition (aucun cimetière éligible) : sur la carte d'arrivée.
		client.spawnPositionMetersX = client.positionMetersX;
		client.spawnPositionMetersY = client.positionMetersY;
		client.spawnPositionMetersZ = client.positionMetersZ;
		PortalNetwork::Disarm(client.portalTravel);
		LOG_INFO(Net, "[ServerApp] Changement de carte (client_id={}, raison={}, {} -> {}, pos=({:.1f}, {:.1f}, {:.1f}))",
			client.clientId, reason,
			cells::MapCellFromZoneId(previousZoneId) ? cells::MapCellLabel(*cells::MapCellFromZoneId(previousZoneId)) : std::to_string(previousZoneId),
			cells::MapCellLabel(cells::MapCellFromZoneId(client.zoneId).value_or(cells::MapCell{})),
			client.positionMetersX, client.positionMetersY, client.positionMetersZ);
		SaveConnectedClient(client, reason);
		// La carte (ZoneChange, canal fiable) puis la position (ForcePosition, renvoyé
		// tant que le client reste dans l'ancienne époque de mouvement).
		SendMapZoneChange(client);
		m_antiCheat.Reset(client.persistenceCharacterKey);
		SendForcePosition(client, kForcePositionReasonTeleport);
		// Comme un téléport MJ : le shard n'a pas le terrain de la nouvelle carte, la
		// hauteur de la commande suivante (client posé au sol) est reprise telle quelle.
		client.movement.trustNextHeight = true;
		UpdateClientInterest(client);
		SendDynamicEventBootstrap(client);
		ApplyQuestEvent(client, QuestStepType::Enter, std::string("zone:") + std::to_string(client.zoneId), 1, "zone_enter");
		return true;
	}

	bool ServerApp::InitCellWorld()
	{
		namespace cells = engine::world::cells;
		const std::string activeZone = m_config.GetString("world.active_zone", "");
		m_cellWorld = cells::IsCellWorld(activeZone);
		m_regionActivity.Begin();
		// ETAPE4.3 — la marge ne descend jamais sous la portée de l'intérêt (± 3 cellules de
		// 100 m, arrondie à 400 m) : un spawner visible d'un joueur est toujours dans une
		// région chaude, donc évalué.
		const double interestReachMeters = static_cast<double>((kBaseInterestRadiusCells + 1) * kCellSizeMeters);
		m_regionActivity.SetMarginMeters((std::max)(interestReachMeters, m_config.GetDouble("world.region_activation_margin_m",
			RegionActivity::kDefaultMarginMeters)));
		if (!m_cellWorld)
		{
			LOG_INFO(Net, "[ServerApp] Monde historique (active_zone='{}') : regions de 10 km inactives", activeZone);
			return true;
		}

		std::string error;
		std::string warning;
		if (!m_worldStarts.LoadForWorld(m_config, activeZone, error, warning))
		{
			LOG_ERROR(Net, "[ServerApp] Monde en cellules : {} (lancer tools/world_gen avant le deploiement)", error);
			return false;
		}
		// Seconde carte (Les Enfers) : absente = pas de départ démon ni de portail (non bloquant).
		if (!warning.empty())
		{
			LOG_WARN(Net, "[ServerApp] Monde en cellules : {} (lancer tools/world_gen --world enfers)", warning);
		}
		if (m_worldStarts.GeneratedCells().empty())
		{
			LOG_ERROR(Net, "[ServerApp] Monde en cellules : start_locations.json sans generated_cells (generateur trop ancien)");
			return false;
		}
		// Le départ par défaut (race inconnue, mode no-DB) doit exister et être jouable :
		// c'est le filet de NormalizeCellWorldLoginPosition.
		const engine::world::StartLocation* fallbackStart = m_worldStarts.Resolve("");
		if (fallbackStart == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Monde en cellules : depart par defaut (default_race) absent");
			return false;
		}
		// Chaque départ doit être jouable : sinon la relocalisation placerait le joueur
		// derrière le mur invisible (boucle de ForcePosition).
		for (const engine::world::StartLocation& start : m_worldStarts.All())
		{
			if (!m_worldStarts.IsGeneratedCell(cells::CellFromPosition(start.x, start.z), start.map))
			{
				LOG_ERROR(Net, "[ServerApp] Monde en cellules : depart '{}' ({}, carte {}) hors des cellules generees",
					start.faction.empty() ? start.race : start.faction, start.cityName, cells::WorldIdOf(start.map));
				return false;
			}
		}
		std::string cellList;
		for (const cells::CellCoord& cell : m_worldStarts.GeneratedCells())
			cellList += (cellList.empty() ? "" : ",") + cells::LoreCellId(cell);
		LOG_INFO(Net, "[ServerApp] Monde en cellules '{}' : {} cellule(s) generee(s) [{}], {} depart(s), marge d'activation {:.0f} m",
			activeZone, m_worldStarts.GeneratedCells().size(), cellList, m_worldStarts.Count(),
			m_regionActivity.MarginMeters());
		if (m_worldStarts.HasMap(cells::CellMap::Enfers))
		{
			LOG_INFO(Net, "[ServerApp] Seconde carte 'enfers' : {} cellule(s) generee(s)",
				m_worldStarts.GeneratedCells(cells::CellMap::Enfers).size());
		}
		// v1.0.124 — lignes maritimes : absentes = aucun bateau (non bloquant) ; invalides = refus.
		{
			std::string seaError;
			if (!m_seaLines.LoadForCellWorld(m_config, seaError))
			{
				LOG_ERROR(Net, "[ServerApp] Lignes maritimes : {}", seaError);
				return false;
			}
			LOG_INFO(Net, "[ServerApp] Lignes maritimes : {} ligne(s) chargee(s)", m_seaLines.Count());
			InitBoats(); // v1.0.125 — bateaux visibles qui suivent les lignes
		}
		// Portails entre cartes : absents = aucun passage (non bloquant) ; invalides = refus.
		std::string portalError;
		std::string portalWarning;
		if (!m_portalNetwork.LoadForCellWorld(m_config, portalError, portalWarning))
		{
			LOG_ERROR(Net, "[ServerApp] Portails entre cartes : {}", portalError);
			return false;
		}
		// Un portail dont l'apparié ou la cellule d'arrivée n'est pas jouable serait un piège.
		size_t usablePortals = 0;
		for (const PortalEndpoint& portal : m_portalNetwork.All())
		{
			const std::optional<PortalDestination> destination = m_portalNetwork.DestinationOf(portal);
			if (!destination || !m_worldStarts.IsGeneratedCell(cells::CellFromPosition(destination->x, destination->z), destination->map))
			{
				LOG_WARN(Net, "[ServerApp] Portail '{}' ({}) : apparie '{}:{}' absent ou hors des cellules generees (inactif)",
					portal.id, cells::WorldIdOf(portal.map), cells::WorldIdOf(portal.linkMap), portal.linkId);
				continue;
			}
			++usablePortals;
		}
		LOG_INFO(Net, "[ServerApp] Portails entre cartes : {} charge(s), {} utilisable(s){}{}",
			m_portalNetwork.Count(), usablePortals, portalWarning.empty() ? "" : " — ", portalWarning);
		return true;
	}

	void ServerApp::ApplyLoadShedding(bool active)
	{
		if (active)
		{
			// Plafond d'entités réduit : seules les plus proches restent (le rayon de vue se
			// réduit de lui-même). Jamais sous 20, jamais au-dessus du plafond nominal (un
			// petit plafond configuré n'est pas relevé).
			const uint32_t reduced = std::max<uint32_t>(20u,
				static_cast<uint32_t>(static_cast<double>(m_viewCapNominal) * m_sheddingViewCapFactor));
			m_viewCapEntities = std::min(m_viewCapNominal, reduced);
			// IA de repos et d'ambiance deux fois moins souvent (le combat ne change pas).
			m_mobActivity = m_mobActivityNominal;
			m_mobActivity.calmHz = std::max(1u, m_mobActivityNominal.calmHz / 2u);
			m_mobActivity.ambientHz = std::max(1u, m_mobActivityNominal.ambientHz / 2u);
		}
		else
		{
			m_viewCapEntities = m_viewCapNominal;
			m_mobActivity = m_mobActivityNominal;
		}
		LOG_WARN(Net, "[ServerApp] Délestage {} (charge lissée {:.0f} % du tick, plafond {} entités/client, IA repos {} Hz / ambiance {} Hz, nouveaux PNJ d'ambiance {})",
			active ? "ACTIVÉ" : "levé", m_loadShedder.SmoothedLoad() * 100.0, m_viewCapEntities,
			m_mobActivity.calmHz, m_mobActivity.ambientHz, active ? "suspendus" : "repris");
	}

	void ServerApp::RefreshRegionActivity()
	{
		if (!m_cellWorld || m_currentTick < m_nextRegionRefreshTick)
		{
			return;
		}
		m_nextRegionRefreshTick = m_currentTick + std::max<uint32_t>(1u, m_tickHz);
		const size_t previousActive = m_regionActivity.ActiveCount();
		m_regionActivity.Begin();
		for (const ConnectedClient& client : m_clients)
		{
			if (client.hasReplicatedState && engine::world::cells::IsCellZoneId(client.zoneId))
			{
				// Seconde carte : la région porte la carte du joueur (Royaumes / Enfers).
				m_regionActivity.AddPlayer(client.positionMetersX, client.positionMetersZ, ClientCellMap(client));
			}
		}
		if (m_regionActivity.ActiveCount() != previousActive)
		{
			LOG_INFO(Net, "[ServerApp] Regions actives : {} (occupees={}, joueurs={})",
				m_regionActivity.ActiveCount(), m_regionActivity.OccupiedCount(), m_clients.size());
		}

		// ETAPE4.3 (chantier 2.1) — cycle de vie des régions : froide après
		// world.region_idle_s sans joueur (ses spawners ne sont plus parcourus),
		// déchargée après world.region_unload_s (délais de réapparition remis à neuf).
		const std::vector<engine::world::cells::MapCell> cold =
			m_regionLifecycle.Update(m_regionActivity.ActiveCells(), std::chrono::steady_clock::now());
		for (const engine::world::cells::MapCell& region : cold)
		{
			LOG_INFO(Net, "[ServerApp] Region froide : {} (plus parcourue)", engine::world::cells::MapCellLabel(region));
		}
		// Déchargement : la région sort du suivi. Ses mobs sont déjà retirés ; les délais
		// de réapparition en cours continuent de courir (les remettre à zéro offrirait un
		// raccourci pour « farmer » un mob rare : s'éloigner 6 min puis revenir).
		for (const engine::world::cells::MapCell& region : m_regionLifecycle.DrainUnloaded())
		{
			LOG_INFO(Net, "[ServerApp] Region dechargee : {}", engine::world::cells::MapCellLabel(region));
		}
		RebuildEvaluatedSpawners();
	}

	void ServerApp::UpdateClientRegion(ConnectedClient& client)
	{
		namespace cells = engine::world::cells;
		if (!m_cellWorld || !cells::IsCellZoneId(client.zoneId))
		{
			return;
		}
		// Seconde carte : la cellule est cherchée sur la carte COURANTE du joueur.
		const uint32_t regionZoneId = cells::CellZoneId(
			cells::CellFromPosition(client.positionMetersX, client.positionMetersZ), ClientCellMap(client));
		if (regionZoneId == client.zoneId)
		{
			return;
		}
		const uint32_t previousZoneId = client.zoneId;
		client.zoneId = regionZoneId;
		// Même carte, même grille : pas de retrait de la grille ni de ZoneChange (le
		// client streame les cellules d'après sa position). Seuls la présence, les
		// quêtes « zone:<id> » et le bootstrap d'événements suivent la région.
		m_shardPresence.UpdateZone(client.accountId != 0 ? client.accountId
			: static_cast<uint64_t>(client.persistenceCharacterKey), client.zoneId);
		LOG_INFO(Net, "[ServerApp] Region changee (client_id={}, {} -> {}, pos=({:.1f}, {:.1f}))",
			client.clientId,
			cells::MapCellFromZoneId(previousZoneId) ? cells::MapCellLabel(*cells::MapCellFromZoneId(previousZoneId)) : std::string("?"),
			cells::MapCellLabel(cells::MapCellFromZoneId(client.zoneId).value_or(cells::MapCell{})),
			client.positionMetersX,
			client.positionMetersZ);
		SendDynamicEventBootstrap(client);
		ApplyQuestEvent(client, QuestStepType::Enter, std::string("zone:") + std::to_string(client.zoneId), 1, "zone_enter");
	}

	bool ServerApp::IsInsideGeneratedWorld(float positionX, float positionZ, engine::world::cells::CellMap map) const
	{
		if (!m_cellWorld)
		{
			return true;
		}
		// Position fournie par le client : bornée AVANT la conversion en indice de
		// cellule (un float énorme rendrait le cast entier indéfini).
		constexpr float kMaxAbsCoordinateMeters = 1.0e7f;
		if (!std::isfinite(positionX) || !std::isfinite(positionZ)
			|| std::fabs(positionX) > kMaxAbsCoordinateMeters || std::fabs(positionZ) > kMaxAbsCoordinateMeters)
		{
			return false;
		}
		return m_worldStarts.IsGeneratedCell(engine::world::cells::CellFromPosition(positionX, positionZ), map);
	}

	engine::world::cells::CellMap ServerApp::ClientCellMap(const ConnectedClient& client)
	{
		return engine::world::cells::CellMapOfZoneOr(client.zoneId);
	}

	bool ServerApp::NormalizeCellWorldLoginPosition(ConnectedClient& client, std::optional<uint32_t> persistedZoneFromFile)
	{
		namespace cells = engine::world::cells;
		if (!m_cellWorld)
		{
			if (cells::IsCellZoneId(client.zoneId))
			{
				// Retour arrière (monde en cellules → zone historique) : la position est
				// globale, hors de la grille ±5000 m de la zone. On revient au point
				// d'apparition historique (mêmes clés et défauts que le master).
				LOG_WARN(Net, "[ServerApp] Personnage sauvegarde dans le monde en cellules, monde historique actif : zone 1, spawn par defaut (client_id={})",
					client.clientId);
				client.zoneId = 1;
				client.positionMetersX = static_cast<float>(m_config.GetDouble("character_creation.default_spawn.x", 0.0));
				client.positionMetersY = static_cast<float>(m_config.GetDouble("character_creation.default_spawn.y", 100.0));
				client.positionMetersZ = static_cast<float>(m_config.GetDouble("character_creation.default_spawn.z", 0.0));
				client.velocityMetersPerSecondX = 0.0f;
				client.velocityMetersPerSecondY = 0.0f;
				client.velocityMetersPerSecondZ = 0.0f;
				return true;
			}
			return false;
		}

		// Ancien repère : le fichier personnage porte une zone historique (feyhin = 1…),
		// la position est LOCALE à cette zone et n'a aucun sens dans le repère global.
		const bool legacyFrame = persistedZoneFromFile.has_value() && !cells::IsCellZoneId(*persistedZoneFromFile);
		// Seconde carte (Les Enfers) — carte du personnage : celle de sa zone persistée ;
		// sans zone (nouveau personnage), celle du départ de sa faction, à défaut de sa
		// race (v1.0.61 ; Légion infernale : Enfers).
		const engine::world::StartLocation* raceStart = m_worldStarts.ResolveFor(client.factionId, client.raceId);
		cells::CellMap map = raceStart != nullptr ? raceStart->map : cells::CellMap::Royaumes;
		if (persistedZoneFromFile.has_value() && cells::IsCellZoneId(*persistedZoneFromFile))
		{
			map = cells::CellMapOfZoneOr(*persistedZoneFromFile);
		}
		const bool inside = IsInsideGeneratedWorld(client.positionMetersX, client.positionMetersZ, map)
			&& std::isfinite(client.positionMetersY);
		bool relocated = false;
		if (legacyFrame || !inside)
		{
			const engine::world::StartLocation* start = raceStart;
			if (start != nullptr)
			{
				LOG_INFO(Net,
					"[ServerApp] Position replacee au depart '{}' (client_id={}, faction='{}', race='{}', carte={}, raison={}, ancienne=({:.1f}, {:.1f}, {:.1f}))",
					start->cityName, client.clientId, client.factionId, client.raceId, cells::WorldIdOf(start->map),
					legacyFrame ? "repere_historique" : "hors_monde_genere",
					client.positionMetersX, client.positionMetersY, client.positionMetersZ);
				map = start->map;
				client.positionMetersX = static_cast<float>(start->x);
				client.positionMetersY = static_cast<float>(start->y);
				client.positionMetersZ = static_cast<float>(start->z);
				client.yawRadians = static_cast<float>(start->yawDeg) * 0.01745329252f;
				client.velocityMetersPerSecondX = 0.0f;
				client.velocityMetersPerSecondY = 0.0f;
				client.velocityMetersPerSecondZ = 0.0f;
				relocated = true;
			}
		}
		// La région se déduit TOUJOURS de la position et de la carte (le fichier et la DB
		// peuvent diverger sur la cellule, jamais sur la carte : elle vient de la zone).
		client.zoneId = cells::CellZoneId(cells::CellFromPosition(client.positionMetersX, client.positionMetersZ), map);
		return relocated;
	}
}
