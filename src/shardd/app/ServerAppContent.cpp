// ServerApp — chargement du contenu au démarrage : spawners, tables de butin, points de réapparition, catalogue de découverte, quêtes, événements dynamiques.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	bool ServerApp::InitSpawners()
	{
		m_mobs.clear();
		m_spawners.clear();
		// ETAPE4.3 — aucune liste ne doit garder d'indice d'un chargement précédent
		// (même sur un retour en erreur plus bas).
		m_spawnersByRegion.clear();
		m_spawnersOutsideCells.clear();
		m_evaluatedSpawners.clear();
		m_evaluatedRegions.clear();
		m_regionLifecycle.Clear();
		// ETAPE4.1 (chantiers 2.2, 2.3) — sommeil et classes de fréquence des mobs.
		{
			mob_activity::MobActivityConfig activity{};
			activity.combatHz = static_cast<uint32_t>(std::clamp<int64_t>(m_config.GetInt("server.mob_ai_hz", 10), 1, 1000));
			activity.calmHz = static_cast<uint32_t>(std::clamp<int64_t>(m_config.GetInt("server.mob_ai.calm_hz", 5), 1, 1000));
			activity.ambientHz = static_cast<uint32_t>(std::clamp<int64_t>(m_config.GetInt("server.mob_ai.ambient_hz", 5), 1, 1000));
			activity.wakeRadiusMeters = static_cast<float>(m_config.GetDouble("server.mob_ai.wake_radius_m", 600.0));
			activity.sleepDelaySeconds = static_cast<float>(m_config.GetDouble("server.mob_ai.sleep_delay_s", 3.0));
			m_mobActivity = mob_activity::Sanitize(activity, static_cast<uint32_t>(m_tickHz));
			// Jamais en deçà de la portée de l'intérêt (cellule du joueur ± 3 cellules de
			// 100 m, soit au plus 4 cellules en diagonale, ≈ 566 m) : un mob encore visible
			// d'un joueur reste éveillé (pas de « statue » sous ses yeux). 0 = sommeil
			// désactivé.
			const float interestReachMeters = static_cast<float>((kBaseInterestRadiusCells + 1) * kCellSizeMeters) * 1.4143f;
			if (m_mobActivity.wakeRadiusMeters > 0.0f && m_mobActivity.wakeRadiusMeters < interestReachMeters)
				m_mobActivity.wakeRadiusMeters = interestReachMeters;
			m_mobActivityNominal = m_mobActivity; // ETAPE4.5 : valeur hors délestage
			LOG_INFO(Net, "[ServerApp] IA des mobs : combat {} Hz, repos {} Hz, ambiance {} Hz, sommeil au-delà de {:.0f} m après {:.1f} s",
				m_mobActivity.combatHz, m_mobActivity.calmHz, m_mobActivity.ambientHz,
				m_mobActivity.wakeRadiusMeters, m_mobActivity.sleepDelaySeconds);
		}
		// ETAPE4.5 (chantier 2.5) — délestage avec hystérésis (durée du tick).
		{
			LoadSheddingConfig shedding{};
			shedding.enabled = m_config.GetBool("server.load_shedding.enabled", true);
			shedding.enterLoad = m_config.GetDouble("server.load_shedding.enter_load", 0.85);
			shedding.exitLoad = m_config.GetDouble("server.load_shedding.exit_load", 0.60);
			shedding.enterHoldSeconds = m_config.GetDouble("server.load_shedding.enter_hold_s", 2.0);
			shedding.exitHoldSeconds = m_config.GetDouble("server.load_shedding.exit_hold_s", 10.0);
			shedding.minDwellSeconds = m_config.GetDouble("server.load_shedding.min_dwell_s", 15.0);
			m_loadShedder.Configure(shedding);
			m_sheddingViewCapFactor = std::clamp(m_config.GetDouble("server.load_shedding.view_cap_factor", 0.5), 0.1, 1.0);
			const LoadSheddingConfig& applied = m_loadShedder.Config();
			LOG_INFO(Net, "[ServerApp] Délestage : {} (entrée ≥ {:.0f} % du tick pendant {:.0f} s, sortie ≤ {:.0f} % pendant {:.0f} s, {:.0f} s entre bascules)",
				applied.enabled ? "actif" : "désactivé", applied.enterLoad * 100.0, applied.enterHoldSeconds,
				applied.exitLoad * 100.0, applied.exitHoldSeconds, applied.minDwellSeconds);
		}
		// ETAPE5.5 (chantier 3.5) — sauvegardes différentielles.
		{
			m_differentialSaves = m_config.GetBool("server.save.differential", true);
			m_savedBlocks.SetFullEvery(static_cast<uint32_t>(std::clamp<int64_t>(m_config.GetInt("server.save.full_every", 20), 1, 1000)));
			LOG_INFO(Net, "[ServerApp] Sauvegardes différentielles : {} (écriture complète toutes les {} sauvegardes)",
				m_differentialSaves ? "actives" : "désactivées", m_savedBlocks.FullEvery());
		}
		// ETAPE5.2 (chantier 1.5) — garde contre les évictions juste après un calage du tick.
		{
			engine::core::StallGuardConfig stall{};
			stall.enabled = m_config.GetBool("server.stall_guard.enabled", true);
			stall.stallSeconds = m_config.GetDouble("server.stall_guard.stall_sec", 3.0);
			stall.graceSeconds = m_config.GetDouble("server.stall_guard.grace_sec", 30.0);
			stall.maxSuspendSeconds = m_config.GetDouble("server.stall_guard.max_suspend_sec", 120.0);
			m_stallGuard.Configure(stall);
			LOG_INFO(Net, "[ServerApp] Garde de calage : {} (calage ≥ {:.1f} s, éviction UDP suspendue {:.0f} s)",
				m_stallGuard.Config().enabled ? "active" : "désactivée", m_stallGuard.Config().stallSeconds,
				m_stallGuard.Config().graceSeconds);
		}
		// ETAPE4.2 (chantier 2.6) — travaux périodiques étalés par identifiant.
		{
			const uint32_t tickHz = static_cast<uint32_t>(m_tickHz);
			m_spawnerEvalPeriodTicks = tick_slicing::PeriodTicks(tickHz, m_config.GetDouble("server.spawners.evaluate_hz", 2.0));
			m_mapDiscoveryPeriodTicks = tick_slicing::PeriodTicks(tickHz, m_config.GetDouble("server.map_discovery_hz", 4.0));
			m_auctionExpiryPeriodTicks = tick_slicing::PeriodTicks(tickHz, 1.0);
			LOG_INFO(Net, "[ServerApp] Travaux étalés : spawners toutes les {} ticks, carte toutes les {} ticks, enchères toutes les {} ticks",
				m_spawnerEvalPeriodTicks, m_mapDiscoveryPeriodTicks, m_auctionExpiryPeriodTicks);
		}
		// Monde vivant — horloge monde du shard (mêmes clés et défauts que le
		// master, cf. masterd/main_linux.cpp) : horaires jour/nuit des PNJ.
		m_worldClockParams.epochRefUnixMs = static_cast<uint64_t>(
			m_config.GetInt("game.worldclock.epoch_ms", 1767225600000ll));
		m_worldClockParams.timeScaleRealMinPerDay = static_cast<float>(
			m_config.GetDouble("game.worldclock.timescale_real_min_per_day", 60.0));
		// Combat SP1 — le catalogue d'archétypes doit être chargé avant les
		// spawners pour valider leurs références (politique stricte : catalogue
		// absent/corrompu = le shard ne boote pas).
		if (!m_archetypeLibrary.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: archetype library load failed");
			return false;
		}

		if (!m_spawnerRuntime.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: runtime load failed");
			return false;
		}

		for (const SpawnerDefinition& definition : m_spawnerRuntime.GetDefinitions())
		{
			// Combat SP1 — tout spawner référençant un archétype inconnu est une
			// erreur de données : échec d'init plutôt qu'un mob aux stats MVP.
			if (m_archetypeLibrary.Find(definition.archetypeId) == nullptr)
			{
				LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: unknown archetype (spawner_id={}, archetype_id={})",
					definition.spawnerId,
					definition.archetypeId);
				m_spawners.clear();
				return false;
			}

			CellGrid* zoneGrid = GetOrCreateZoneGrid(definition.zoneId);
			if (zoneGrid == nullptr)
			{
				LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: missing zone grid (spawner_id={}, zone_id={})",
					definition.spawnerId,
					definition.zoneId);
				m_spawners.clear();
				return false;
			}

			SpawnerRuntimeState runtimeState{};
			runtimeState.definition = definition;
			runtimeState.slots.resize(definition.count);
			// Monde « Royaumes » — région du spawner = cellule de SA position (le zoneId
			// du fichier est celui de la cellule qui l'a généré : ils coïncident).
			runtimeState.inCellWorld = engine::world::cells::IsCellZoneId(definition.zoneId);
			if (runtimeState.inCellWorld)
			{
				// Seconde carte : la région porte la carte de la zone (cell_* / enfers_*).
				runtimeState.regionCell = engine::world::cells::MapCell(
					engine::world::cells::CellFromPosition(definition.positionMetersX, definition.positionMetersZ),
					engine::world::cells::CellMapOfZoneOr(definition.zoneId));
			}
			if (!zoneGrid->TryWorldToCellCoord(definition.positionMetersX, definition.positionMetersZ, runtimeState.centerCell))
			{
				LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: invalid spawn position (spawner_id={}, zone_id={})",
					definition.spawnerId,
					definition.zoneId);
				m_spawners.clear();
				return false;
			}

			// ETAPE4.4 — un agenda ne vaut que pour un PNJ d'ambiance : sur une créature
			// (IA de combat), il est ignoré et signalé.
			if (runtimeState.definition.agenda.enabled)
			{
				const CreatureArchetype* arch = m_archetypeLibrary.Find(runtimeState.definition.archetypeId);
				if (arch == nullptr || !arch->ambient)
				{
					LOG_WARN(Net, "[ServerApp] Spawner '{}' : agenda ignoré (archétype {} non « ambient »)",
						runtimeState.definition.spawnerId, runtimeState.definition.archetypeId);
					runtimeState.definition.agenda.enabled = false;
				}
			}
			// POP.1 — classe de population : déclarée (validée par la règle d'or) ou
			// déduite de l'archétype. Une déclaration écartée est signalée une fois ici.
			{
				const CreatureArchetype* arch = m_archetypeLibrary.Find(runtimeState.definition.archetypeId);
				const bool ambientArch = arch != nullptr && arch->ambient;
				const bool attackableArch = arch == nullptr || arch->attackable;
				const bool raceHostileArch = arch != nullptr && !arch->hostileToRaces.empty();
				bool rejected = false;
				runtimeState.populationClass = engine::world::ResolveShardPopulationClass(
					runtimeState.definition.populationClass,
					engine::world::InferShardPopulationClass(ambientArch, attackableArch, raceHostileArch),
					attackableArch, raceHostileArch, rejected);
				if (rejected)
				{
					LOG_WARN(Net, "[ServerApp] Spawner '{}' : populationClass {} refusée (archétype {} attaquable ou hostile : un figurant n'a aucun effet de jeu) → {}",
						runtimeState.definition.spawnerId,
						engine::world::PopulationClassLetter(*runtimeState.definition.populationClass),
						runtimeState.definition.archetypeId,
						engine::world::PopulationClassLetter(runtimeState.populationClass));
				}
			}
			m_spawners.push_back(std::move(runtimeState));
			// FIX.6 — DEBUG : une ligne par spawner = 56 716 lignes au démarrage du monde
			// « royaumes » (7,8 Mo de log). Résumé unique en INFO après la boucle.
			LOG_DEBUG(Net, "[ServerApp] Spawner ready (id={}, zone_id={}, count={}, respawn_sec={})",
				definition.spawnerId,
				definition.zoneId,
				definition.count,
				definition.respawnSeconds);
		}

		{
			// FIX.6 — résumé du chargement (le détail par spawner est en DEBUG).
			size_t totalSlots = 0;
			size_t withAgenda = 0;
			engine::world::PopulationCounts slotsByClass{}; // POP.1
			for (const SpawnerRuntimeState& spawner : m_spawners)
			{
				totalSlots += spawner.slots.size();
				withAgenda += spawner.definition.agenda.enabled ? 1u : 0u;
				slotsByClass[engine::world::PopulationClassIndex(spawner.populationClass)] += static_cast<uint32_t>(spawner.slots.size());
			}
			LOG_INFO(Net, "[ServerApp] Spawners prets : {} spawner(s), {} emplacement(s) de mob, {} avec agenda (detail : log.level=debug)",
				m_spawners.size(), totalSlots, withAgenda);
			LOG_INFO(Net, "[ServerApp] POP.1 classes de population (emplacements) : A={} B={} C={}",
				slotsByClass[0], slotsByClass[1], slotsByClass[2]);
		}

		// ETAPE4.3 (chantier 2.1) — index des spawners par région : seules les régions
		// chaudes sont parcourues par UpdateSpawners.
		m_spawnersByRegion.clear();
		m_spawnersOutsideCells.clear();
		for (size_t index = 0; index < m_spawners.size(); ++index)
		{
			if (m_spawners[index].inCellWorld)
				m_spawnersByRegion[RegionKey(m_spawners[index].regionCell)].push_back(index);
			else
				m_spawnersOutsideCells.push_back(index);
		}
		{
			GridStateConfig regionTimers{};
			regionTimers.idleTimeout = std::chrono::seconds{ std::clamp<int64_t>(m_config.GetInt("world.region_idle_s", 60), 0, 86400) };
			regionTimers.unloadTimeout = std::chrono::seconds{ std::clamp<int64_t>(m_config.GetInt("world.region_unload_s", 300), 0, 86400) };
			m_regionLifecycle.Configure(regionTimers);
			m_defaultMobMoveSpeed = static_cast<float>(m_config.GetDouble("server.mob_move_speed_meters_per_second", kDefaultMobMoveSpeedMetersPerSecond));
			m_spawnerDespawnGraceTicks = static_cast<uint32_t>(std::clamp<double>(
				m_config.GetDouble("server.spawners.despawn_grace_s", 20.0), 0.0, 600.0) * static_cast<double>(m_tickHz));
		}
		m_evaluatedRegions.clear();
		RebuildEvaluatedSpawners();
		LOG_INFO(Net, "[ServerApp] Spawner init OK (spawners={}, regions={}, hors_cellules={}, retrait après {} ticks sans joueur, région froide après {} s, déchargée après {} s)",
			m_spawners.size(), m_spawnersByRegion.size(), m_spawnersOutsideCells.size(), m_spawnerDespawnGraceTicks,
			m_regionLifecycle.Config().idleTimeout.count(), m_regionLifecycle.Config().unloadTimeout.count());
		return true;
	}

	uint32_t ServerApp::RegionKey(const engine::world::cells::MapCell& region)
	{
		// Identifiant de zone de la cellule : unique entre les cartes (Royaumes / Enfers).
		return engine::world::cells::MapCellZoneId(region);
	}

	void ServerApp::RebuildEvaluatedSpawners()
	{
		m_evaluatedSpawners.assign(m_spawnersOutsideCells.begin(), m_spawnersOutsideCells.end());
		std::unordered_set<uint32_t> regions;
		auto addRegion = [&](uint32_t key, const std::vector<size_t>& indices)
		{
			if (!regions.insert(key).second)
				return;
			m_evaluatedSpawners.insert(m_evaluatedSpawners.end(), indices.begin(), indices.end());
		};
		for (const auto& [key, indices] : m_spawnersByRegion)
		{
			const engine::world::cells::MapCell region =
				engine::world::cells::MapCellFromZoneId(key).value_or(engine::world::cells::MapCell{});
			// Hors monde en cellules (pas de régions actives) : tout est évalué, comme avant.
			if (!m_cellWorld || m_regionLifecycle.IsWarm(region))
			{
				addRegion(key, indices);
				continue;
			}
			// Région froide déjà parcourue : gardée tant qu'un de ses mobs vit encore
			// (combat en cours, retrait différé) ; les autres ne coûtent rien.
			if (m_evaluatedRegions.count(key) == 0u)
				continue;
			const bool hasLiveMob = std::any_of(indices.begin(), indices.end(), [this](size_t index)
			{
				const SpawnerRuntimeState& spawner = m_spawners[index];
				return std::any_of(spawner.slots.begin(), spawner.slots.end(),
					[](const SpawnerSlotState& slot) { return slot.mobEntityId != 0; });
			});
			if (hasLiveMob)
				addRegion(key, indices);
		}
		std::sort(m_evaluatedSpawners.begin(), m_evaluatedSpawners.end());
		m_evaluatedRegions = std::move(regions);
	}

	bool ServerApp::InitLootTables()
	{
		m_lootTableEntries.clear();

		const std::string relativePath = m_config.GetString("server.loot_table_path", "loot/loot_tables.txt");
		const std::string lootTableText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
		if (lootTableText.empty())
		{
			LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: empty or missing file (path={})", relativePath);
			return false;
		}

		std::istringstream input(lootTableText);
		std::string line;
		uint32_t lineNumber = 0;
		while (std::getline(input, line))
		{
			++lineNumber;
			std::string_view lineView(line);
			if (lineView.empty() || lineView.front() == '#')
			{
				continue;
			}

			std::istringstream lineStream(line);
			LootTableEntry entry{};
			std::string visibilityToken;
			if (!(lineStream >> entry.sourceArchetypeId >> entry.item.itemId >> entry.item.quantity >> visibilityToken))
			{
				LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: invalid line {} in {}", lineNumber, relativePath);
				m_lootTableEntries.clear();
				return false;
			}

			if (!TryParseLootVisibility(visibilityToken, entry.visibility))
			{
				LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: invalid visibility '{}' at line {}", visibilityToken, lineNumber);
				m_lootTableEntries.clear();
				return false;
			}

			if (entry.sourceArchetypeId == 0 || entry.item.itemId == 0 || entry.item.quantity == 0)
			{
				LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: zero value at line {}", lineNumber);
				m_lootTableEntries.clear();
				return false;
			}

			// Loot réel (2026-07-19) — colonnes OPTIONNELLES rétro-compatibles :
			//   [drop_chance_pct]           1..100 (absent = 100, toujours)
			//   [min_count max_count]       quantité uniforme (absent = fixe)
			uint32_t chance = 0;
			if (lineStream >> chance)
			{
				if (chance == 0 || chance > 100)
				{
					LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: drop_chance {} hors [1..100] ligne {}",
						chance, lineNumber);
					m_lootTableEntries.clear();
					return false;
				}
				entry.dropChancePct = static_cast<uint8_t>(chance);
				uint32_t minCount = 0;
				uint32_t maxCount = 0;
				if (lineStream >> minCount >> maxCount)
				{
					if (minCount == 0 || maxCount < minCount)
					{
						LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: min/max ({}/{}) invalides ligne {}",
							minCount, maxCount, lineNumber);
						m_lootTableEntries.clear();
						return false;
					}
					entry.minCount = minCount;
					entry.maxCount = maxCount;
				}
			}

			m_lootTableEntries.push_back(entry);
		}

		if (m_lootTableEntries.empty())
		{
			LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: no entries loaded (path={})", relativePath);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Loot table init OK (path={}, entries={})", relativePath, m_lootTableEntries.size());
		return true;
	}

	void ServerApp::InitRespawnPoints()
	{
		m_respawnPoints.clear();

		// Respawn points de la zone active (game/data/zones/<zone>/respawn_points.txt) si
		// world.active_zone est défini, sinon repli sur l'ancien chemin global (rétro-compat).
		const std::string activeZone = m_config.GetString("world.active_zone", "");
		// Monde « Royaumes » : les points de chaque cellule générée sont concaténés
		// (positions globales, zoneId = cellule) — le plus proche gagne au respawn.
		std::vector<std::string> relativePaths;
		if (m_cellWorld)
		{
			// Seconde carte : les cellules des Enfers ont aussi leurs points (cimetière
			// neutre de la Citadelle) ; SameMap les réserve aux joueurs de leur carte.
			for (int m = 0; m < engine::world::cells::kCellMapCount; ++m)
			{
				const engine::world::cells::CellMap map = static_cast<engine::world::cells::CellMap>(m);
				for (const engine::world::cells::CellCoord& cell : m_worldStarts.GeneratedCells(map))
					relativePaths.push_back("zones/" + engine::world::cells::CellDirName(cell, map) + "/respawn_points.txt");
			}
		}
		else
		{
			relativePaths.push_back(activeZone.empty()
				? m_config.GetString("server.respawn_points_path", "respawn/respawn_points.txt")
				: ("zones/" + activeZone + "/respawn_points.txt"));
		}
		// Rayon neutre par défaut (surchargé par colonne par cimetière). Ex-clé globals.
		const float defaultNeutralRadiusM = static_cast<float>(
			m_config.GetDouble("globals.graveyard_default_faction_neutral_radius_m", 500.0));
		for (const std::string& relativePath : relativePaths)
		{
			const std::string pointsText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
			if (pointsText.empty())
			{
				LOG_WARN(Net, "[ServerApp] Respawn points absent ({}): repli sur le point d'entrée en monde", relativePath);
				continue;
			}

			std::istringstream input(pointsText);
			std::string line;
			uint32_t lineNumber = 0;
			while (std::getline(input, line))
			{
				++lineNumber;
				if (line.empty() || line[0] == '#')
					continue;

				std::istringstream lineStream(line);
				RespawnPointDefinition point{};
				std::string typeToken;
				if (!(lineStream >> point.zoneId >> typeToken
					>> point.positionMetersX >> point.positionMetersY >> point.positionMetersZ))
				{
					LOG_WARN(Net, "[ServerApp] Respawn points: ligne {} invalide ignorée ({})", lineNumber, relativePath);
					continue;
				}
				if (typeToken == "graveyard")
					point.destinationType = kRespawnDestinationGraveyard;
				else if (typeToken == "inn")
					point.destinationType = kRespawnDestinationInn;
				else
				{
					LOG_WARN(Net, "[ServerApp] Respawn points: type '{}' inconnu ligne {} (attendu graveyard|inn)", typeToken, lineNumber);
					continue;
				}
				// Colonnes optionnelles (cimetières) : faction propriétaire puis rayon neutre,
				// et (lore v1.0.114) identifiant stable `gy=<id>` en dernier jeton.
				// Absentes → cimetière neutre (faction vide) avec le rayon par défaut.
				std::string rest;
				std::getline(lineStream, rest);
				const RespawnTrailingTokens tokens = ParseRespawnTrailingTokens(rest);
				point.ownerFactionId = tokens.faction;
				point.neutralRadiusM = tokens.hasNeutralRadius ? tokens.neutralRadiusM : defaultNeutralRadiusM;
				if (point.destinationType == kRespawnDestinationGraveyard)
					point.graveyardId = tokens.graveyardId;
				m_respawnPoints.push_back(point);
			}
		}

		LOG_INFO(Net, "[ServerApp] Respawn points init OK (points={}, files={})", m_respawnPoints.size(), relativePaths.size());
	}

	void ServerApp::InitDiscoveryCatalog()
	{
		m_discoveryCatalog = DiscoveryCatalog{};
		// Positions globales seulement dans le monde en cellules (zone historique : rien).
		if (!m_cellWorld)
		{
			return;
		}
		// Cimetières des Royaumes identifiés (gy=) ; ceux des Enfers ne comptent pas.
		std::vector<GraveyardSite> graveyards;
		size_t withoutId = 0;
		for (const RespawnPointDefinition& point : m_respawnPoints)
		{
			if (point.destinationType != kRespawnDestinationGraveyard
				|| engine::world::cells::CellMapOfZoneId(point.zoneId) != engine::world::cells::CellMap::Royaumes)
				continue;
			if (point.graveyardId.empty())
			{
				++withoutId;
				continue;
			}
			graveyards.push_back({ point.graveyardId, point.positionMetersX, point.positionMetersZ });
		}
		const size_t duplicates = m_discoveryCatalog.SetGraveyards(std::move(graveyards));
		// Lieux secrets : fichiers réservés au serveur (absents = aucun secret sur la carte).
		for (int m = 0; m < engine::world::cells::kCellMapCount; ++m)
		{
			const std::string world(engine::world::cells::WorldIdOf(static_cast<engine::world::cells::CellMap>(m)));
			const std::string text = engine::platform::FileSystem::ReadAllTextContent(m_config,
				"world/" + world + "/secret_places.json");
			std::string error;
			if (!text.empty() && !m_discoveryCatalog.AppendSecretPlacesFromText(text, error))
				LOG_WARN(Net, "[ServerApp] Lieux secrets ({}) illisibles : {}", world, error);
		}
		LOG_INFO(Net, "[ServerApp] Exploration (v1.0.114) : {} cimetiere(s) identifie(s) dans les Royaumes ({} sans id, {} en double), "
			"{} lieu(x) secret(s), exploit des {} cimetieres {}",
			m_discoveryCatalog.GraveyardCount(), withoutId, duplicates, m_discoveryCatalog.SecretPlaceCount(),
			kPilgrimGraveyardCount, m_discoveryCatalog.PilgrimExploitPossible() ? "actif" : "INACTIF (nombre different)");
	}

	bool ServerApp::InitQuests()
	{
		if (!m_questRuntime.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Quest init FAILED");
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Quest init OK");
		return true;
	}

	bool ServerApp::InitDynamicEvents()
	{
		m_dynamicEvents.clear();
		if (!m_eventRuntime.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event init FAILED: runtime load failed");
			return false;
		}

		for (const DynamicEventDefinition& definition : m_eventRuntime.GetDefinitions())
		{
			DynamicEventState state{};
			state.definition = definition;
			ScheduleDynamicEventTrigger(state, false);
			m_dynamicEvents.push_back(std::move(state));
			LOG_INFO(Net, "[ServerApp] Dynamic event ready (event_id={}, zone_id={}, trigger={}, cooldown_sec={})",
				definition.eventId,
				definition.zoneId,
				GetDynamicEventTriggerTypeName(definition.triggerType),
				definition.cooldownSeconds);
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event init OK (events={})", m_dynamicEvents.size());
		return true;
	}
}
