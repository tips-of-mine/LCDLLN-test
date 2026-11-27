// ServerApp — monstres : IA, agendas et routines d'ambiance, spawners (apparition / disparition), mobs MJ, événements dynamiques.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	uint32_t ServerApp::ResolveMobAiIntervalTicks() const
	{
		// Perf : appelée pour chaque mob à chaque pas d'IA ; la config et m_tickHz ne
		// changent plus après Init -> valeur calculée une fois puis mise en cache.
		if (m_mobAiIntervalTicksCache != 0u && m_mobAiIntervalTicksCacheHz == m_tickHz)
			return m_mobAiIntervalTicksCache;
		const uint32_t configuredAiHz = static_cast<uint32_t>(std::clamp<int64_t>(
			m_config.GetInt("server.mob_ai_hz", 10),
			1,
			static_cast<int64_t>(m_tickHz)));
		m_mobAiIntervalTicksCache = std::max<uint32_t>(1u, static_cast<uint32_t>(m_tickHz) / configuredAiHz);
		m_mobAiIntervalTicksCacheHz = m_tickHz;
		return m_mobAiIntervalTicksCache;
	}

	void ServerApp::UpdateMobAi()
	{
		if (m_mobs.empty())
		{
			return;
		}

		const uint32_t tickHz = static_cast<uint32_t>(m_tickHz);
		for (MobEntity& mob : m_mobs)
		{
			if ((mob.stateFlags & kEntityStateDead) != 0u)
			{
				continue;
			}

			// ETAPE4.1 (chantier 2.2) — sommeil. Occupé (dégâts, menace, provocation
			// reçus entre deux vérifications) : réveil immédiat. Sinon, voisinage vérifié
			// une fois par seconde, étalé par identifiant d'entité.
			// Un PNJ d'ambiance qui rentre chez lui (fin de son horaire) compte aussi :
			// endormi en route, il ne serait jamais retiré ni replacé à sa porte.
			const bool busy = mob_activity::IsBusy(mob.aggroTargetEntityId != 0, !mob.threatTable.empty(),
				mob.aiState == MobAiState::Aggro || mob.aiState == MobAiState::Return
					|| (mob.ambient && mob.routine.goingHome && !mob.scheduleRetired));
			if (mob_activity::WakeIfBusy(mob.sleep, busy))
			{
				mob.lastAiTick = m_currentTick; // pas de rattrapage du temps dormi
			}
			else if (mob_activity::IsWakeCheckTick(mob.entityId, m_currentTick, tickHz))
			{
				const mob_activity::SleepTransition transition = mob_activity::UpdateSleep(mob.sleep,
					IsPlayerNearMob(mob, m_mobActivity.wakeRadiusMeters), busy, m_currentTick, mob.spawnTick,
					tickHz, m_mobActivity);
				if (transition == mob_activity::SleepTransition::FellAsleep)
				{
					// Immobile pour les clients (sinon ils prolongeraient sa dernière vitesse).
					mob.velocityMetersPerSecondX = 0.0f;
					mob.velocityMetersPerSecondY = 0.0f;
					mob.velocityMetersPerSecondZ = 0.0f;
				}
				else if (transition == mob_activity::SleepTransition::WokeUp)
				{
					mob.lastAiTick = m_currentTick;
					mob.nextAiTick = m_currentTick;
				}
			}
			if (mob.sleep.asleep)
			{
				continue;
			}

			if (m_currentTick < mob.nextAiTick)
			{
				continue;
			}

			// ETAPE4.1 (chantier 2.3) — fréquence selon la classe (combat, repos,
			// ambiance) ; le pas couvre le temps réellement écoulé.
			const uint32_t interval = mob_activity::IntervalTicks(
				mob_activity::Classify(mob.ambient, busy), m_mobActivity, tickHz);
			mob.aiStepSeconds = mob_activity::CatchUpSeconds(mob.lastAiTick, m_currentTick, interval, tickHz, m_mobActivity);
			mob.lastAiTick = m_currentTick;
			mob.nextAiTick = m_currentTick + interval;
			UpdateMobAi(mob);
		}
	}

	void ServerApp::UpdateMobAi(MobEntity& mob)
	{
		// Monde vivant — les PNJ d'ambiance suivent leur routine, sans aggro. Seconde
		// carte : un PNJ hostile à certaines races (gardes de la Légion contre les
		// humains) passe à l'IA de combat ci-dessous tant qu'il combat ou regagne son poste.
		if (mob.ambient && (!mob.raceHostile || !EngageHostileRaces(mob)))
		{
			UpdateAmbientMob(mob);
			return;
		}
		RefreshMobAggroTarget(mob);
		if (mob.aggroTargetEntityId != 0)
		{
			SetMobAiState(mob, MobAiState::Aggro);
		}

		switch (mob.aiState)
		{
		case MobAiState::Idle:
			if (mob.aggroTargetEntityId == 0 && m_currentTick >= mob.nextPatrolTick)
			{
				SetMobAiState(mob, MobAiState::Patrol);
			}
			break;

		case MobAiState::Patrol:
		{
			// Validation v12 — ERRANCE ALÉATOIRE : l'ancien aller-retour linéaire
			// (cible fixe home+5 m, pause 0,2 s) faisait pivoter le mob sur place
			// plusieurs fois par seconde (demi-tours incessants, amplifiés par la
			// séparation entre mobs). Désormais : cible aléatoire autour du point
			// d'attache + vraie pause (2 à 6 s) à l'arrivée — déplacement naturel.
			if (MoveMobTowards(mob, mob.patrolTargetMetersX, mob.patrolTargetMetersZ))
			{
				const float wanderAngle = NextCombatRoll01() * 6.2831853f;
				const float wanderRadius = 1.5f
					+ NextCombatRoll01() * std::max(0.5f, kDefaultMobPatrolDistanceMeters - 1.5f);
				mob.patrolTargetMetersX = mob.homePositionMetersX + std::cos(wanderAngle) * wanderRadius;
				mob.patrolTargetMetersZ = mob.homePositionMetersZ + std::sin(wanderAngle) * wanderRadius;
				mob.velocityMetersPerSecondX = 0.0f;
				mob.velocityMetersPerSecondZ = 0.0f;
				// Pause 2-6 s avant la prochaine errance (en ticks d'IA).
				const uint32_t pauseAiTicks = 20u + static_cast<uint32_t>(NextCombatRoll01() * 40.0f);
				mob.nextPatrolTick = m_currentTick + ResolveMobAiIntervalTicks() * pauseAiTicks;
				SetMobAiState(mob, MobAiState::Idle);
			}
			break;
		}

		case MobAiState::Aggro:
		{
			ConnectedClient* target = FindClientByEntityId(mob.aggroTargetEntityId);
			if (target == nullptr
				|| !target->hasReplicatedState
				|| !engine::world::cells::SameMap(target->zoneId, mob.zoneId)
				|| target->stats.currentHealth == 0
				|| (target->stateFlags & kEntityStateDead) != 0u)
			{
				LOG_INFO(Net, "[ServerApp] Mob aggro target lost (mob_entity_id={}, target_entity_id={})",
					mob.entityId,
					mob.aggroTargetEntityId);
				ResetMobThreat(mob);
				SetMobAiState(mob, MobAiState::Return);
				break;
			}

			const float homeDistanceSquared = DistanceSquaredXZ(
				target->positionMetersX,
				target->positionMetersZ,
				mob.homePositionMetersX,
				mob.homePositionMetersZ);
			const float leashDistanceSquared = mob.leashDistanceMeters * mob.leashDistanceMeters;
			if (homeDistanceSquared > leashDistanceSquared)
			{
				LOG_INFO(Net, "[ServerApp] Mob leash triggered (mob_entity_id={}, target_entity_id={}, distance_sq={:.2f}, leash_sq={:.2f})",
					mob.entityId,
					target->entityId,
					homeDistanceSquared,
					leashDistanceSquared);
				ResetMobThreat(mob);
				SetMobAiState(mob, MobAiState::Return);
				break;
			}

			if (!TryMobAttackPlayer(mob, *target))
			{
				// Validation v12 — distance d'ARRÊT : le mob s'approche jusqu'à
				// ~80 % de sa portée d'attaque mais ne marche jamais DANS le
				// joueur (la superposition géographique provoquait un mob
				// « collé » au même point que sa cible).
				const float chaseDx = target->positionMetersX - mob.positionMetersX;
				const float chaseDz = target->positionMetersZ - mob.positionMetersZ;
				const float chaseDist = std::sqrt(chaseDx * chaseDx + chaseDz * chaseDz);
				const float stopDistance = std::max(1.0f, mob.combat.attackRangeMeters * 0.8f);
				if (chaseDist > stopDistance)
				{
					const float chaseRatio = (chaseDist - stopDistance) / chaseDist;
					(void)MoveMobTowards(mob,
						mob.positionMetersX + chaseDx * chaseRatio,
						mob.positionMetersZ + chaseDz * chaseRatio);
				}
			}
			break;
		}

		case MobAiState::Return:
			if (MoveMobTowards(mob, mob.homePositionMetersX, mob.homePositionMetersZ))
			{
				mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
				SetMobAiState(mob, MobAiState::Idle);
			}
			break;
		}
	}

	bool ServerApp::EngageHostileRaces(MobEntity& mob)
	{
		const CreatureArchetype* arch = m_archetypeLibrary.Find(mob.archetypeId);
		if (arch == nullptr || arch->hostileToRaces.empty())
		{
			return false;
		}
		// Déjà en combat (menace, cible) ou en route vers son poste : IA de combat.
		if (!mob.threatTable.empty() || mob.aggroTargetEntityId != 0
			|| mob.aiState == MobAiState::Aggro || mob.aiState == MobAiState::Return)
		{
			return true;
		}
		if (mob.scheduleRetired)
		{
			return false; // rentré chez lui (hors horaire) : n'agresse personne
		}
		// Agression à vue : joueur le plus proche d'une race hostile dans le rayon.
		const float radiusSq = arch->aggroRadiusMeters * arch->aggroRadiusMeters;
		const ConnectedClient* best = nullptr;
		float bestSq = radiusSq;
		for (const ConnectedClient& client : m_clients)
		{
			if (!client.hasReplicatedState || !engine::world::cells::SameMap(client.zoneId, mob.zoneId)
				|| client.stats.currentHealth == 0 || (client.stateFlags & kEntityStateDead) != 0u
				|| client.gm.invisible || !arch->IsHostileToRace(client.raceId))
			{
				continue;
			}
			const float d2 = DistanceSquaredXZ(client.positionMetersX, client.positionMetersZ,
				mob.positionMetersX, mob.positionMetersZ);
			if (d2 <= bestSq)
			{
				bestSq = d2;
				best = &client;
			}
		}
		if (best == nullptr)
		{
			return false;
		}
		// Menace minimale : la riposte du joueur (dégâts) prendra vite le dessus.
		mob.threatTable.push_back(ThreatEntry{ best->entityId, 1u });
		LOG_INFO(Net, "[ServerApp] PNJ hostile a la race '{}' : agression (mob_entity_id={}, archetype={}, client_id={}, distance={:.1f} m)",
			best->raceId, mob.entityId, mob.archetypeId, best->clientId, std::sqrt(bestSq));
		return true;
	}

	bool ServerApp::IsMobAttackableBy(const MobEntity& mob, const ConnectedClient& attacker) const
	{
		if (mob.attackable)
		{
			return true;
		}
		if (!mob.raceHostile)
		{
			return false;
		}
		const CreatureArchetype* arch = m_archetypeLibrary.Find(mob.archetypeId);
		return arch != nullptr && arch->CanBeAttackedByRace(attacker.raceId);
	}

	float ServerApp::CurrentWorldHour() const
	{
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
		return engine::world::TimeOfDayHours(engine::world::GameSeconds(nowMs, m_worldClockParams));
	}

	float ServerApp::ResolveSpawnerMobSpeed(const SpawnerRuntimeState& spawner) const
	{
		// Même résolution qu'à l'apparition (SpawnMobFromSpawner) : vitesse de l'archétype
		// si elle est donnée, sinon la vitesse par défaut des mobs (config, lue à l'init).
		if (const CreatureArchetype* arch = m_archetypeLibrary.Find(spawner.definition.archetypeId);
			arch != nullptr && arch->moveSpeedMetersPerSecond > 0.0f)
			return arch->moveSpeedMetersPerSecond;
		return m_defaultMobMoveSpeed;
	}

	ambient::AgendaState ServerApp::EvaluateSpawnerAgenda(const SpawnerRuntimeState& spawner, uint32_t slotIndex,
		double gameSeconds) const
	{
		const ambient::AgendaDefinition& agenda = spawner.definition.agenda;
		ambient::RoutinePoint home{ spawner.definition.positionMetersX, spawner.definition.positionMetersZ };
		if (!agenda.places[static_cast<size_t>(ambient::AgendaPlace::Home)].empty())
			home = agenda.places[static_cast<size_t>(ambient::AgendaPlace::Home)].front();
		return ambient::EvaluateAgenda(agenda, ambient::AgendaSeed(spawner.definition.spawnerId, slotIndex),
			gameSeconds, ResolveSpawnerMobSpeed(spawner), GameSecondsPerRealSecond(), home);
	}

	double ServerApp::CurrentGameSeconds() const
	{
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
		return engine::world::GameSeconds(nowMs, m_worldClockParams);
	}

	double ServerApp::GameSecondsPerRealSecond() const
	{
		// Même repli que l'horloge monde (WorldClock.h) : ≤ 0 → 60 min réelles par jour.
		const double configured = static_cast<double>(m_worldClockParams.timeScaleRealMinPerDay);
		const double realMinutesPerDay = configured > 0.0 ? configured : 60.0;
		return 86400.0 / (realMinutesPerDay * 60.0);
	}

	const ambient::AgendaDefinition* ServerApp::FindMobAgenda(const MobEntity& mob) const
	{
		if (mob.isDynamicEventMob || mob.owningSpawnerIndex >= m_spawners.size()) return nullptr;
		const ambient::AgendaDefinition& agenda = m_spawners[mob.owningSpawnerIndex].definition.agenda;
		return agenda.enabled ? &agenda : nullptr;
	}

	void ServerApp::UpdateAgendaMob(MobEntity& mob, const ambient::AgendaDefinition& agenda)
	{
		// ETAPE4.4 (chantier 2.7) — l'agenda dit où le PNJ doit être à cette heure ; le
		// shard ne fait que l'y conduire. Aucun tirage au hasard : tout vient de la graine.
		constexpr float kArriveRadiusMeters = 1.4f;
		constexpr float kHomeRadiusMeters = 4.0f;
		const double gameSeconds = CurrentGameSeconds();
		ambient::RoutinePoint fallback;
		fallback.x = mob.homePositionMetersX;
		fallback.z = mob.homePositionMetersZ;
		const ambient::AgendaState state = ambient::EvaluateAgenda(agenda, mob.agendaSeed, gameSeconds,
			mob.moveSpeedMetersPerSecond, GameSecondsPerRealSecond(), fallback);
		// La nuit, il rentre : compté « occupé » (4.1) jusqu'au retrait, jamais endormi en route.
		mob.routine.goingHome = state.activity == ambient::AgendaActivity::Sleep;
		const ambient::RoutinePoint target = ambient::StrollPoint(agenda, mob.agendaSeed, state, gameSeconds);
		(void)MoveMobTowards(mob, target.x, target.z);
		// Chez lui : rayon élargi (plusieurs PNJ partagent souvent une porte, la séparation
		// entre mobs les écarte d'1,2 m), et retrait de toute façon une fois l'heure
		// d'arrivée passée (state.indoors) : un PNJ bloqué en chemin ne veille jamais.
		const float radius = mob.routine.goingHome ? kHomeRadiusMeters : kArriveRadiusMeters;
		const bool arrived = DistanceSquaredXZ(mob.positionMetersX, mob.positionMetersZ, target.x, target.z) < radius * radius;
		if (arrived || (mob.routine.goingHome && state.indoors))
		{
			mob.velocityMetersPerSecondX = 0.0f;
			mob.velocityMetersPerSecondZ = 0.0f;
			if (mob.routine.goingHome)
				mob.scheduleRetired = true; // chez lui : retiré, réapparaîtra au matin
		}
	}

	const ambient::RoutineDefinition* ServerApp::FindMobRoutine(const MobEntity& mob) const
	{
		if (mob.isDynamicEventMob || mob.owningSpawnerIndex >= m_spawners.size()) return nullptr;
		const ambient::RoutineDefinition& def = m_spawners[mob.owningSpawnerIndex].definition.routine;
		return def.kind == ambient::RoutineKind::None ? nullptr : &def;
	}

	void ServerApp::UpdateAmbientMob(MobEntity& mob)
	{
		// Un PNJ d'ambiance ne garde jamais de menace ni d'effets (non attaquable).
		mob.threatTable.clear();
		mob.aggroTargetEntityId = 0;
		mob.auras.clear();
		const ambient::RoutineDefinition* def = FindMobRoutine(mob);
		auto roll = [](void* ctx) { return static_cast<ServerApp*>(ctx)->NextCombatRoll01(); };

		// 1) Un joueur tout proche (dialogue, curiosité) : le PNJ s'arrête et le regarde.
		constexpr float kTalkPauseRadiusMeters = 2.6f;
		const ConnectedClient* nearest = nullptr;
		float nearestD2 = kTalkPauseRadiusMeters * kTalkPauseRadiusMeters;
		for (const ConnectedClient& client : m_clients)
		{
			if (!client.hasReplicatedState || !engine::world::cells::SameMap(client.zoneId, mob.zoneId)) continue;
			const float d2 = DistanceSquaredXZ(client.positionMetersX, client.positionMetersZ, mob.positionMetersX, mob.positionMetersZ);
			if (d2 < nearestD2) { nearestD2 = d2; nearest = &client; }
		}
		if (nearest != nullptr)
		{
			mob.velocityMetersPerSecondX = 0.0f;
			mob.velocityMetersPerSecondZ = 0.0f;
			mob.yawRadians = std::atan2(nearest->positionMetersX - mob.positionMetersX, nearest->positionMetersZ - mob.positionMetersZ);
			// Reprend sa route un peu après le départ du joueur.
			mob.routine.pauseUntilTick = std::max(mob.routine.pauseUntilTick, m_currentTick + static_cast<uint32_t>(m_tickHz));
			return;
		}
		// ETAPE4.4 — PNJ à agenda de métier : il suit sa journée (prioritaire sur la routine).
		if (const ambient::AgendaDefinition* agenda = FindMobAgenda(mob))
		{
			if (m_currentTick < mob.routine.pauseUntilTick)
			{
				mob.velocityMetersPerSecondX = 0.0f;
				mob.velocityMetersPerSecondZ = 0.0f;
				return; // vient de parler à un joueur : reprend un peu après
			}
			UpdateAgendaMob(mob, *agenda);
			return;
		}
		if (def == nullptr)
		{
			// Ambiance sans routine : simple présence, immobile.
			mob.velocityMetersPerSecondX = 0.0f;
			mob.velocityMetersPerSecondZ = 0.0f;
			return;
		}

		// Rayons d'arrivée > séparation des mobs (1,2 m, UpdateMobSpatialState) :
		// deux PNJ partageant une porte ou un point de rue ne peuvent pas occuper
		// le même point — sans cette marge, le second « marcherait sur place ».
		constexpr float kArriveRadiusMeters = 1.4f;
		constexpr float kHomeRadiusMeters = 1.5f;

		// 2 bis) v1.0.142 — routine à altitude réelle (ville-tour) : le PNJ rentre en refaisant
		//     son chemin à l'envers (paliers et volées), jamais en ligne droite à travers les
		//     étages ; son altitude suit le segment parcouru.
		if (def->realHeight)
		{
			if (mob.routine.goingHome && !mob.routine.homeRetrace)
				ambient::BeginHomeRetrace(*def, mob.routine, mob.homePositionMetersX, mob.homePositionMetersZ);
			else if (!mob.routine.goingHome && mob.routine.homeRetrace)
				mob.routine.homeRetrace = false; // horaire rouvert : il reprend sa tournée d'ici
			if (mob.routine.homeRetrace)
			{
				(void)MoveMobTowards(mob, mob.routine.target.x, mob.routine.target.z);
				mob.positionMetersY = ambient::RoutineHeightAlong(mob.routine.origin, mob.routine.target,
					mob.positionMetersX, mob.positionMetersZ);
				if (DistanceSquaredXZ(mob.positionMetersX, mob.positionMetersZ, mob.routine.target.x, mob.routine.target.z) < kArriveRadiusMeters * kArriveRadiusMeters
					&& ambient::AdvanceHomeRetrace(*def, mob.routine))
				{
					mob.velocityMetersPerSecondX = 0.0f;
					mob.velocityMetersPerSecondZ = 0.0f;
					mob.scheduleRetired = true;
				}
				return;
			}
		}

		// 2) Hors horaire : retour au logis (sa porte), puis retrait.
		if (mob.routine.goingHome)
		{
			const ambient::RoutinePoint home = ambient::HomePoint(*def, mob.homePositionMetersX, mob.homePositionMetersZ);
			(void)MoveMobTowards(mob, home.x, home.z);
			if (DistanceSquaredXZ(mob.positionMetersX, mob.positionMetersZ, home.x, home.z) < kHomeRadiusMeters * kHomeRadiusMeters)
			{
				mob.velocityMetersPerSecondX = 0.0f;
				mob.velocityMetersPerSecondZ = 0.0f;
				mob.scheduleRetired = true;
			}
			return;
		}

		// 3) Pause en cours (contemplation, marchandage, conversation…).
		if (m_currentTick < mob.routine.pauseUntilTick)
		{
			mob.velocityMetersPerSecondX = 0.0f;
			mob.velocityMetersPerSecondZ = 0.0f;
			if (def->kind == ambient::RoutineKind::Post && def->hasFacePoint)
				mob.yawRadians = std::atan2(def->facePoint.x - mob.positionMetersX, def->facePoint.z - mob.positionMetersZ);
			return;
		}

		// 4) Marche vers la cible courante ; à l'arrivée : pause éventuelle puis cible suivante.
		if (!mob.routine.hasTarget)
			ambient::ChooseNextTarget(*def, mob.routine, mob.homePositionMetersX, mob.homePositionMetersZ, roll, this);
		(void)MoveMobTowards(mob, mob.routine.target.x, mob.routine.target.z);
		// v1.0.142 — altitude réelle : interpolée sur le segment origine → cible.
		if (def->realHeight)
			mob.positionMetersY = ambient::RoutineHeightAlong(mob.routine.origin, mob.routine.target,
				mob.positionMetersX, mob.positionMetersZ);
		if (DistanceSquaredXZ(mob.positionMetersX, mob.positionMetersZ, mob.routine.target.x, mob.routine.target.z) < kArriveRadiusMeters * kArriveRadiusMeters)
		{
			mob.velocityMetersPerSecondX = 0.0f;
			mob.velocityMetersPerSecondZ = 0.0f;
			const float pauseSec = ambient::RollPauseSeconds(*def, roll, this);
			if (pauseSec > 0.0f)
				mob.routine.pauseUntilTick = m_currentTick + static_cast<uint32_t>(pauseSec * static_cast<float>(m_tickHz));
			ambient::ChooseNextTarget(*def, mob.routine, mob.homePositionMetersX, mob.homePositionMetersZ, roll, this);
		}
	}

	bool ServerApp::IsSpawnerActivatedByPlayers(const SpawnerRuntimeState& spawner) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (!client.hasReplicatedState || !client.hasCell
				|| !engine::world::cells::SameMap(client.zoneId, spawner.definition.zoneId))
			{
				continue;
			}

			const int dx = std::abs(static_cast<int>(client.currentCell.x) - static_cast<int>(spawner.centerCell.x));
			const int dz = std::abs(static_cast<int>(client.currentCell.z) - static_cast<int>(spawner.centerCell.z));
			if (dx <= kBaseInterestRadiusCells && dz <= kBaseInterestRadiusCells)
			{
				return true;
			}
			// Monde vivant — un PNJ en routine (voyageur, patrouille) ou à agenda (4.4) peut
			// s'être éloigné de son spawner : il reste actif tant qu'un joueur le voit.
			if (spawner.definition.routine.kind != ambient::RoutineKind::None || spawner.definition.agenda.enabled)
			{
				for (const SpawnerSlotState& slot : spawner.slots)
				{
					if (slot.mobEntityId == 0) continue;
					const MobEntity* mob = FindMobByEntityId(slot.mobEntityId);
					if (mob == nullptr) continue;
					const CellGrid* grid = nullptr;
					if (auto it = m_zoneGrids.find(engine::world::cells::MapIdOfZone(mob->zoneId)); it != m_zoneGrids.end()) grid = &it->second;
					CellCoord mc{};
					if (grid != nullptr && grid->TryWorldToCellCoord(mob->positionMetersX, mob->positionMetersZ, mc)
						&& std::abs(static_cast<int>(client.currentCell.x) - static_cast<int>(mc.x)) <= kBaseInterestRadiusCells
						&& std::abs(static_cast<int>(client.currentCell.z) - static_cast<int>(mc.z)) <= kBaseInterestRadiusCells)
						return true;
				}
			}
		}

		return false;
	}

	bool ServerApp::IsPlayerNearMob(const MobEntity& mob, float radiusMeters) const
	{
		if (radiusMeters <= 0.0f)
			return true;
		const float radiusSquared = radiusMeters * radiusMeters;
		for (const ConnectedClient& client : m_clients)
		{
			if (!client.hasReplicatedState || !engine::world::cells::SameMap(client.zoneId, mob.zoneId))
				continue;
			if (DistanceSquaredXZ(client.positionMetersX, client.positionMetersZ, mob.positionMetersX, mob.positionMetersZ)
				<= radiusSquared)
				return true;
		}
		return false;
	}

	bool ServerApp::SpawnerHasCombat(const SpawnerRuntimeState& spawner) const
	{
		for (const SpawnerSlotState& slot : spawner.slots)
		{
			if (slot.mobEntityId == 0)
			{
				continue;
			}

			const MobEntity* mob = FindMobByEntityId(slot.mobEntityId);
			if (mob == nullptr || (mob->stateFlags & kEntityStateDead) != 0u)
			{
				continue;
			}

			if (mob->aggroTargetEntityId != 0 || !mob->threatTable.empty() || mob->aiState == MobAiState::Aggro)
			{
				return true;
			}
		}

		return false;
	}

	bool ServerApp::SpawnMobFromSpawner(SpawnerRuntimeState& spawner, uint32_t slotIndex)
	{
		if (slotIndex >= spawner.slots.size())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob spawn FAILED: invalid slot (spawner_id={}, slot={})",
				spawner.definition.spawnerId,
				slotIndex);
			return false;
		}

		CellGrid* zoneGrid = GetOrCreateZoneGrid(spawner.definition.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob spawn FAILED: zone grid unavailable (spawner_id={}, zone_id={})",
				spawner.definition.spawnerId,
				spawner.definition.zoneId);
			return false;
		}

		MobEntity mob{};
		mob.entityId = m_nextServerEntityId++;
		mob.zoneId = spawner.definition.zoneId;
		mob.archetypeId = spawner.definition.archetypeId;
		mob.positionMetersX = spawner.definition.positionMetersX;
		mob.positionMetersY = spawner.definition.positionMetersY;
		mob.positionMetersZ = spawner.definition.positionMetersZ;
		// Combat SP1 — stats data-driven (archétype validé à l'init des spawners).
		ApplyArchetypeStatsToMob(mob, m_archetypeLibrary.Find(mob.archetypeId), m_tickHz);
		mob.homePositionMetersX = mob.positionMetersX;
		mob.homePositionMetersY = mob.positionMetersY;
		mob.homePositionMetersZ = mob.positionMetersZ;
		mob.patrolTargetMetersX = mob.positionMetersX + kDefaultMobPatrolDistanceMeters;
		mob.patrolTargetMetersZ = mob.positionMetersZ;
		mob.leashDistanceMeters = spawner.definition.leashDistanceMeters;
		mob.moveSpeedMetersPerSecond = static_cast<float>(m_config.GetDouble("server.mob_move_speed_meters_per_second", kDefaultMobMoveSpeedMetersPerSecond));
		mob.aiState = MobAiState::Idle;
		mob.nextAiTick = m_currentTick + ResolveMobAiIntervalTicks();
		mob.spawnTick = m_currentTick; // ETAPE4.1 : départ du délai de sommeil
		mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
		mob.owningSpawnerIndex = static_cast<uint32_t>(&spawner - m_spawners.data());
		mob.owningSpawnerSlot = slotIndex;
		mob.populationClass = spawner.populationClass; // POP.1 (résolue à l'init)
		// Monde vivant — comportement d'archétype (PNJ d'ambiance, vitesse propre)
		// et point d'apparition de la routine (sa porte = 1er point du chemin).
		if (const CreatureArchetype* arch = m_archetypeLibrary.Find(mob.archetypeId); arch != nullptr)
		{
			mob.ambient = arch->ambient;
			mob.attackable = arch->attackable;
			mob.raceHostile = !arch->hostileToRaces.empty(); // Enfers : gardes de la Légion
			if (arch->moveSpeedMetersPerSecond > 0.0f)
				mob.moveSpeedMetersPerSecond = arch->moveSpeedMetersPerSecond;
		}
		if (mob.ambient && spawner.definition.routine.kind != ambient::RoutineKind::None)
		{
			const ambient::RoutinePoint home = ambient::HomePoint(spawner.definition.routine,
				spawner.definition.positionMetersX, spawner.definition.positionMetersZ);
			mob.positionMetersX = home.x;
			mob.positionMetersZ = home.z;
			mob.homePositionMetersX = home.x;
			mob.homePositionMetersZ = home.z;
			// v1.0.142 — altitude réelle : il apparaît à l'altitude de sa porte (palier) et le
			// client le pose sur la surface marchable, pas sur la heightmap.
			if (spawner.definition.routine.realHeight && !spawner.definition.routine.path.empty())
			{
				mob.positionMetersY = spawner.definition.routine.path.front().y;
				mob.homePositionMetersY = mob.positionMetersY;
				mob.stateFlags |= kEntityStateRealHeight;
			}
			// Petite désynchronisation : les PNJ d'un même quartier ne repartent pas en même temps.
			mob.routine.pauseUntilTick = m_currentTick + static_cast<uint32_t>(NextCombatRoll01() * 3.0f * static_cast<float>(m_tickHz));
		}
		// ETAPE4.4 (chantiers 2.7, 2.4) — PNJ à agenda : il apparaît LÀ OÙ SA JOURNÉE LE
		// PLACE à cet instant (au travail, à l'auberge, en chemin), pas à sa porte.
		if (mob.ambient && spawner.definition.agenda.enabled)
		{
			mob.agendaSeed = ambient::AgendaSeed(spawner.definition.spawnerId, slotIndex);
			const ambient::RoutinePoint home = spawner.definition.agenda.places[static_cast<size_t>(ambient::AgendaPlace::Home)].empty()
				? ambient::RoutinePoint{ spawner.definition.positionMetersX, spawner.definition.positionMetersZ }
				: spawner.definition.agenda.places[static_cast<size_t>(ambient::AgendaPlace::Home)].front();
			const ambient::AgendaState state = ambient::EvaluateAgenda(spawner.definition.agenda, mob.agendaSeed,
				CurrentGameSeconds(), mob.moveSpeedMetersPerSecond, GameSecondsPerRealSecond(), home);
			mob.positionMetersX = state.position.x;
			mob.positionMetersZ = state.position.z;
			mob.homePositionMetersX = home.x;
			mob.homePositionMetersZ = home.z;
			mob.routine.pauseUntilTick = 0;
		}

		CellCoord mappedCell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, mappedCell))
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob spawn FAILED: grid mapping failed (spawner_id={}, slot={}, entity_id={})",
				spawner.definition.spawnerId,
				slotIndex,
				mob.entityId);
			return false;
		}

		spawner.slots[slotIndex].mobEntityId = mob.entityId;
		spawner.slots[slotIndex].nextRespawnTick = 0;
		m_mobs.push_back(mob);
		LOG_DEBUG(Net,
			"[ServerApp] Spawner mob spawned (spawner_id={}, slot={}, entity_id={}, zone_id={}, archetype_id={})",
			spawner.definition.spawnerId,
			slotIndex,
			mob.entityId,
			mob.zoneId,
			mob.archetypeId);
		return true;
	}

	EntityId ServerApp::SpawnGmMob(uint32_t archetypeId, uint32_t zoneId, float x, float y, float z)
	{
		const CreatureArchetype* arch = m_archetypeLibrary.Find(archetypeId);
		if (arch == nullptr)
			return 0;
		CellGrid* zoneGrid = GetOrCreateZoneGrid(zoneId);
		if (zoneGrid == nullptr)
			return 0;
		MobEntity mob{};
		mob.entityId = m_nextServerEntityId++;
		mob.zoneId = zoneId;
		mob.archetypeId = archetypeId;
		mob.positionMetersX = x;
		mob.positionMetersY = y;
		mob.positionMetersZ = z;
		ApplyArchetypeStatsToMob(mob, arch, m_tickHz);
		mob.homePositionMetersX = x;
		mob.homePositionMetersY = y;
		mob.homePositionMetersZ = z;
		mob.patrolTargetMetersX = x + kDefaultMobPatrolDistanceMeters;
		mob.patrolTargetMetersZ = z;
		mob.leashDistanceMeters = 40.0f;
		mob.moveSpeedMetersPerSecond = arch->moveSpeedMetersPerSecond > 0.0f ? arch->moveSpeedMetersPerSecond
			: static_cast<float>(m_config.GetDouble("server.mob_move_speed_meters_per_second", kDefaultMobMoveSpeedMetersPerSecond));
		mob.aiState = MobAiState::Idle;
		mob.nextAiTick = m_currentTick + ResolveMobAiIntervalTicks();
		mob.spawnTick = m_currentTick;
		mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
		// Hors spawner : indice invalide (routine et agenda ignorés, cf. FindMobRoutine).
		mob.owningSpawnerIndex = std::numeric_limits<uint32_t>::max();
		mob.ambient = arch->ambient;
		mob.attackable = arch->attackable;
		mob.raceHostile = !arch->hostileToRaces.empty(); // Enfers : gardes de la Légion
		mob.gmSpawned = true;
		// POP.1 — mob MJ : pas de déclaration, classe déduite de l'archétype.
		mob.populationClass = engine::world::InferShardPopulationClass(mob.ambient, mob.attackable, mob.raceHostile);
		CellCoord mappedCell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, mappedCell))
			return 0;
		m_mobs.push_back(mob);
		m_gmMobIds.push_back(mob.entityId);
		LOG_INFO(Net, "[ServerApp] ADM.6 mob MJ apparu (entity_id={}, archetype_id={}, zone_id={}, pos=({:.1f},{:.1f},{:.1f}))",
			mob.entityId, archetypeId, zoneId, x, y, z);
		return mob.entityId;
	}

	void ServerApp::PurgeDeadGmMobs()
	{
		for (size_t i = 0; i < m_gmMobIds.size();)
		{
			const MobEntity* mob = FindMobByEntityId(m_gmMobIds[i]);
			if (mob == nullptr)
			{
				m_gmMobIds.erase(m_gmMobIds.begin() + static_cast<std::ptrdiff_t>(i));
				continue;
			}
			if (mob->pendingDespawn)
			{
				(void)RemoveMobByGm(m_gmMobIds[i]); // retire aussi l'entrée de m_gmMobIds
				continue;
			}
			++i;
		}
	}

	bool ServerApp::RemoveMobByGm(EntityId entityId)
	{
		MobEntity* mob = FindMobByEntityId(entityId);
		if (mob == nullptr)
			return false;
		if (!mob->gmSpawned && !mob->isDynamicEventMob && mob->owningSpawnerIndex < m_spawners.size())
		{
			SpawnerRuntimeState& spawner = m_spawners[mob->owningSpawnerIndex];
			if (mob->owningSpawnerSlot < spawner.slots.size() && spawner.slots[mob->owningSpawnerSlot].mobEntityId == entityId)
				return DespawnSpawnerMob(spawner, mob->owningSpawnerSlot, "gm_delete", true);
		}
		if (mob->isDynamicEventMob && mob->owningEventIndex < m_dynamicEvents.size())
			return DespawnDynamicEventMob(m_dynamicEvents[mob->owningEventIndex], entityId, "gm_delete");
		PurgeThreatForEntity(entityId);
		if (CellGrid* zoneGrid = GetOrCreateZoneGrid(mob->zoneId); zoneGrid != nullptr)
			(void)zoneGrid->RemoveEntity(entityId);
		m_mobs.erase(std::remove_if(m_mobs.begin(), m_mobs.end(),
			[entityId](const MobEntity& m) { return m.entityId == entityId; }), m_mobs.end());
		m_gmMobIds.erase(std::remove(m_gmMobIds.begin(), m_gmMobIds.end(), entityId), m_gmMobIds.end());
		return true;
	}

	bool ServerApp::DespawnSpawnerMob(SpawnerRuntimeState& spawner, uint32_t slotIndex, std::string_view reason, bool scheduleRespawn)
	{
		if (slotIndex >= spawner.slots.size())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob despawn FAILED: invalid slot (spawner_id={}, slot={})",
				spawner.definition.spawnerId,
				slotIndex);
			return false;
		}

		SpawnerSlotState& slot = spawner.slots[slotIndex];
		if (slot.mobEntityId == 0)
		{
			LOG_WARN(Net, "[ServerApp] Spawner mob despawn ignored: empty slot (spawner_id={}, slot={}, reason={})",
				spawner.definition.spawnerId,
				slotIndex,
				reason);
			return false;
		}

		const EntityId entityId = slot.mobEntityId;
		if (MobEntity* mob = FindMobByEntityId(entityId); mob != nullptr)
		{
			if (CellGrid* zoneGrid = GetOrCreateZoneGrid(mob->zoneId); zoneGrid != nullptr)
			{
				(void)zoneGrid->RemoveEntity(entityId);
			}
		}

		m_mobs.erase(
			std::remove_if(
				m_mobs.begin(),
				m_mobs.end(),
				[entityId](const MobEntity& mob)
				{
					return mob.entityId == entityId;
				}),
			m_mobs.end());

		slot.mobEntityId = 0;
		slot.nextRespawnTick = scheduleRespawn
			? (m_currentTick + std::max<uint32_t>(1u, spawner.definition.respawnSeconds * static_cast<uint32_t>(m_tickHz)))
			: 0u;
		LOG_DEBUG(Net,
			"[ServerApp] Spawner mob despawned (spawner_id={}, slot={}, entity_id={}, reason={}, next_respawn_tick={})",
			spawner.definition.spawnerId,
			slotIndex,
			entityId,
			reason,
			slot.nextRespawnTick);
		return true;
	}

	void ServerApp::UpdateSpawners()
	{
		if (m_spawners.empty())
		{
			return;
		}

		// Monde vivant — heure de jeu (horaires des PNJ d'ambiance), une fois par passe.
		const float worldHour = CurrentWorldHour();
		const double gameSeconds = CurrentGameSeconds(); // ETAPE4.4 : agendas
		// ETAPE4.3 (chantier 2.1) — seuls les spawners des régions chaudes (et de celles
		// qui ont encore un mob) sont parcourus : une région froide ne coûte rien.
		for (const size_t spawnerIndex : m_evaluatedSpawners)
		{
			// ETAPE4.2 (chantier 2.6) — chaque spawner est évalué une fois par période
			// (server.spawners.evaluate_hz), à son tour : la charge (activation par les
			// joueurs, apparitions, retraits) se répartit sur la période au lieu de tout
			// faire à chaque tick. Une apparition ou un retrait arrive au plus une
			// période plus tard.
			if (!tick_slicing::IsSliceTurn(spawnerIndex, m_currentTick, m_spawnerEvalPeriodTicks))
			{
				continue;
			}
			SpawnerRuntimeState& spawner = m_spawners[spawnerIndex];
			// Monde « Royaumes » — région inactive (aucun joueur dans la cellule ni à moins
			// de la marge de son bord) et aucun mob vivant : coût nul, on passe.
			if (spawner.inCellWorld && !m_regionActivity.IsActive(spawner.regionCell))
			{
				const bool hasLiveMob = std::any_of(spawner.slots.begin(), spawner.slots.end(),
					[](const SpawnerSlotState& slot) { return slot.mobEntityId != 0; });
				if (!hasLiveMob)
				{
					spawner.isActive = false;
					continue;
				}
			}
			const bool shouldBeActive = IsSpawnerActivatedByPlayers(spawner);
			if (spawner.isActive != shouldBeActive)
			{
				spawner.isActive = shouldBeActive;
				if (!shouldBeActive)
					spawner.inactiveSinceTick = m_currentTick; // ETAPE4.3 : début du délai de grâce
				LOG_DEBUG(Net, "[ServerApp] Spawner activation updated (spawner_id={}, active={})",
					spawner.definition.spawnerId,
					spawner.isActive ? "true" : "false");
			}
			const ambient::RoutineDefinition& routine = spawner.definition.routine;
			// ETAPE4.4 — un agenda remplace l'horaire grossier de la routine (jour, nuit).
			const bool hasAgenda = spawner.definition.agenda.enabled;
			const bool scheduled = !hasAgenda && routine.kind != ambient::RoutineKind::None && routine.schedule != ambient::RoutineSchedule::Always;
			const bool scheduleOpen = !scheduled || ambient::IsScheduleOpen(routine.schedule, worldHour);

			for (uint32_t slotIndex = 0; slotIndex < spawner.slots.size(); ++slotIndex)
			{
				SpawnerSlotState& slot = spawner.slots[slotIndex];
				if (slot.mobEntityId == 0)
				{
					continue;
				}

				MobEntity* mob = FindMobByEntityId(slot.mobEntityId);
				if (mob == nullptr)
				{
					slot.mobEntityId = 0;
					continue;
				}

				if (mob->pendingDespawn)
				{
					(void)DespawnSpawnerMob(spawner, slotIndex, "death", true);
					continue;
				}

				// Monde vivant — rentré chez lui hors horaire : retiré sans délai
				// (il réapparaîtra à sa porte à la réouverture de son horaire).
				if (mob->scheduleRetired)
				{
					(void)DespawnSpawnerMob(spawner, slotIndex, "schedule", false);
					continue;
				}
				if (scheduled)
					mob->routine.goingHome = !scheduleOpen;
				// ETAPE4.4 — l'heure du coucher vaut aussi pour un PNJ endormi (4.1) : rentrer
				// le rend « occupé », donc il se réveille et rentre chez lui.
				if (hasAgenda && mob->ambient)
					mob->routine.goingHome = EvaluateSpawnerAgenda(spawner, slotIndex, gameSeconds).activity
						== ambient::AgendaActivity::Sleep;

				if (!spawner.isActive)
				{
					// ETAPE4.3 (chantier 2.4) — délai de grâce : un joueur qui oscille en
					// bordure de portée ne fait pas disparaître et réapparaître les mobs.
					if (m_currentTick - spawner.inactiveSinceTick < m_spawnerDespawnGraceTicks)
					{
						continue;
					}
					if (mob->aggroTargetEntityId != 0 || !mob->threatTable.empty() || mob->aiState == MobAiState::Aggro)
					{
						LOG_DEBUG(Net, "[ServerApp] Spawner despawn delayed: mob still in combat (spawner_id={}, entity_id={})",
							spawner.definition.spawnerId,
							mob->entityId);
						continue;
					}

					(void)DespawnSpawnerMob(spawner, slotIndex, "inactive", false);
				}
			}

			if (!spawner.isActive || SpawnerHasCombat(spawner))
			{
				continue;
			}

			for (uint32_t slotIndex = 0; slotIndex < spawner.slots.size(); ++slotIndex)
			{
				SpawnerSlotState& slot = spawner.slots[slotIndex];
				if (slot.mobEntityId != 0)
				{
					continue;
				}
				if (slot.nextRespawnTick != 0 && m_currentTick < slot.nextRespawnTick)
				{
					continue;
				}
				if (!scheduleOpen)
				{
					continue; // hors horaire : le PNJ reste chez lui
				}
				// ETAPE4.5 — délestage : plus de nouveau PNJ d'ambiance (les présents restent).
				if (m_loadShedder.Active())
				{
					if (const CreatureArchetype* arch = m_archetypeLibrary.Find(spawner.definition.archetypeId);
						arch != nullptr && arch->ambient)
					{
						continue;
					}
				}
				// ETAPE4.4 — PNJ à agenda qui dort chez lui à cette heure : pas d'entité.
				if (hasAgenda && EvaluateSpawnerAgenda(spawner, slotIndex, gameSeconds).indoors)
				{
					continue;
				}

				(void)SpawnMobFromSpawner(spawner, slotIndex);
			}
		}
	}

	void ServerApp::ScheduleDynamicEventTrigger(DynamicEventState& eventState, bool fromCooldown)
	{
		const uint32_t triggerDelayTicksBase = std::max<uint32_t>(1u, eventState.definition.triggerSeconds * static_cast<uint32_t>(m_tickHz));
		uint32_t triggerDelayTicks = triggerDelayTicksBase;
		if (eventState.definition.triggerType == DynamicEventTriggerType::Random)
		{
			const size_t hashValue = std::hash<std::string>{}(eventState.definition.eventId)
				^ static_cast<size_t>(m_currentTick + eventState.definition.cooldownSeconds);
			triggerDelayTicks = 1u + static_cast<uint32_t>(hashValue % triggerDelayTicksBase);
		}

		if (fromCooldown)
		{
			eventState.status = DynamicEventStatus::Cooldown;
			eventState.cooldownUntilTick = m_currentTick + std::max<uint32_t>(1u, eventState.definition.cooldownSeconds * static_cast<uint32_t>(m_tickHz));
			eventState.nextTriggerTick = eventState.cooldownUntilTick + triggerDelayTicks;
		}
		else
		{
			eventState.status = DynamicEventStatus::Idle;
			eventState.cooldownUntilTick = 0;
			eventState.nextTriggerTick = m_currentTick + triggerDelayTicks;
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event trigger scheduled (event_id={}, status={}, next_trigger_tick={})",
			eventState.definition.eventId,
			GetDynamicEventStatusName(eventState.status),
			eventState.nextTriggerTick);
	}

	bool ServerApp::StartDynamicEvent(DynamicEventState& eventState)
	{
		if (eventState.definition.phases.empty())
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event start FAILED: no phases (event_id={})", eventState.definition.eventId);
			return false;
		}

		eventState.status = DynamicEventStatus::Active;
		eventState.currentPhaseIndex = 0;
		eventState.currentPhaseProgress = 0;
		eventState.phaseMobEntityIds.clear();
		eventState.participantClientIds.clear();
		LOG_INFO(Net, "[ServerApp] Dynamic event started (event_id={}, zone_id={})",
			eventState.definition.eventId,
			eventState.definition.zoneId);
		BroadcastDynamicEventState(eventState, eventState.definition.startNotificationText, 0, 0, 0, {});
		return SpawnDynamicEventPhase(eventState);
	}

	bool ServerApp::SpawnDynamicEventPhase(DynamicEventState& eventState)
	{
		if (eventState.currentPhaseIndex >= eventState.definition.phases.size())
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event phase spawn FAILED: invalid phase index (event_id={}, phase={})",
				eventState.definition.eventId,
				eventState.currentPhaseIndex);
			return false;
		}

		eventState.currentPhaseProgress = 0;
		eventState.phaseMobEntityIds.clear();
		const DynamicEventPhaseDefinition& phase = eventState.definition.phases[eventState.currentPhaseIndex];
		for (const DynamicEventSpawnDefinition& spawn : phase.spawns)
		{
			for (uint32_t spawnOrdinal = 0; spawnOrdinal < spawn.count; ++spawnOrdinal)
			{
				if (!SpawnMobForDynamicEvent(eventState, eventState.currentPhaseIndex, spawn, spawnOrdinal))
				{
					LOG_ERROR(Net, "[ServerApp] Dynamic event phase spawn FAILED (event_id={}, phase_id={})",
						eventState.definition.eventId,
						phase.phaseId);
					return false;
				}
			}
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event phase started (event_id={}, phase_id={}, mobs={})",
			eventState.definition.eventId,
			phase.phaseId,
			eventState.phaseMobEntityIds.size());
		BroadcastDynamicEventState(eventState, phase.notificationText, 0, 0, 0, {});
		return true;
	}

	bool ServerApp::SpawnMobForDynamicEvent(
		DynamicEventState& eventState,
		uint32_t phaseIndex,
		const DynamicEventSpawnDefinition& spawnDefinition,
		uint32_t spawnOrdinal)
	{
		CellGrid* zoneGrid = GetOrCreateZoneGrid(eventState.definition.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event mob spawn FAILED: zone grid unavailable (event_id={}, zone_id={})",
				eventState.definition.eventId,
				eventState.definition.zoneId);
			return false;
		}

		MobEntity mob{};
		mob.entityId = m_nextServerEntityId++;
		mob.zoneId = eventState.definition.zoneId;
		mob.archetypeId = spawnDefinition.archetypeId;
		mob.positionMetersX = spawnDefinition.positionMetersX;
		mob.positionMetersY = spawnDefinition.positionMetersY;
		mob.positionMetersZ = spawnDefinition.positionMetersZ;
		// Combat SP1 — stats data-driven ; un événement peut référencer un
		// archétype absent du catalogue (non validé à l'init) → fallback MVP.
		ApplyArchetypeStatsToMob(mob, m_archetypeLibrary.Find(mob.archetypeId), m_tickHz);
		mob.homePositionMetersX = mob.positionMetersX;
		mob.homePositionMetersY = mob.positionMetersY;
		mob.homePositionMetersZ = mob.positionMetersZ;
		mob.patrolTargetMetersX = mob.positionMetersX + kDefaultMobPatrolDistanceMeters;
		mob.patrolTargetMetersZ = mob.positionMetersZ;
		mob.leashDistanceMeters = spawnDefinition.leashDistanceMeters;
		mob.moveSpeedMetersPerSecond = static_cast<float>(m_config.GetDouble("server.mob_move_speed_meters_per_second", kDefaultMobMoveSpeedMetersPerSecond));
		mob.aiState = MobAiState::Idle;
		mob.nextAiTick = m_currentTick + ResolveMobAiIntervalTicks();
		mob.spawnTick = m_currentTick; // ETAPE4.1 : départ du délai de sommeil
		mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
		mob.isDynamicEventMob = true;
		mob.owningEventIndex = static_cast<uint32_t>(&eventState - m_dynamicEvents.data());
		mob.owningEventPhaseIndex = phaseIndex;

		CellCoord mappedCell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, mappedCell))
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event mob spawn FAILED: grid mapping failed (event_id={}, entity_id={})",
				eventState.definition.eventId,
				mob.entityId);
			return false;
		}

		m_mobs.push_back(mob);
		eventState.phaseMobEntityIds.push_back(mob.entityId);
		LOG_INFO(Net,
			"[ServerApp] Dynamic event mob spawned (event_id={}, phase={}, entity_id={}, archetype_id={}, ordinal={})",
			eventState.definition.eventId,
			phaseIndex,
			mob.entityId,
			mob.archetypeId,
			spawnOrdinal);
		return true;
	}

	bool ServerApp::DespawnDynamicEventMob(DynamicEventState& eventState, EntityId entityId, std::string_view reason)
	{
		if (MobEntity* mob = FindMobByEntityId(entityId); mob != nullptr)
		{
			if (CellGrid* zoneGrid = GetOrCreateZoneGrid(mob->zoneId); zoneGrid != nullptr)
			{
				(void)zoneGrid->RemoveEntity(entityId);
			}
		}

		m_mobs.erase(
			std::remove_if(
				m_mobs.begin(),
				m_mobs.end(),
				[entityId](const MobEntity& mob)
				{
					return mob.entityId == entityId;
				}),
			m_mobs.end());

		eventState.phaseMobEntityIds.erase(
			std::remove(eventState.phaseMobEntityIds.begin(), eventState.phaseMobEntityIds.end(), entityId),
			eventState.phaseMobEntityIds.end());

		LOG_INFO(Net, "[ServerApp] Dynamic event mob despawned (event_id={}, entity_id={}, reason={})",
			eventState.definition.eventId,
			entityId,
			reason);
		return true;
	}

	bool ServerApp::AdvanceDynamicEventPhase(DynamicEventState& eventState)
	{
		if ((eventState.currentPhaseIndex + 1u) >= eventState.definition.phases.size())
		{
			CompleteDynamicEvent(eventState);
			return true;
		}

		++eventState.currentPhaseIndex;
		eventState.currentPhaseProgress = 0;
		eventState.phaseMobEntityIds.clear();
		LOG_INFO(Net, "[ServerApp] Dynamic event phase advanced (event_id={}, phase_index={})",
			eventState.definition.eventId,
			eventState.currentPhaseIndex);
		return SpawnDynamicEventPhase(eventState);
	}

	void ServerApp::CompleteDynamicEvent(DynamicEventState& eventState)
	{
		const DynamicEventReward rewards = eventState.definition.rewards;
		eventState.status = DynamicEventStatus::Cooldown;
		BroadcastDynamicEventState(eventState, eventState.definition.completionNotificationText, 0, 0, 0, {});
		for (uint32_t participantClientId : eventState.participantClientIds)
		{
			for (ConnectedClient& client : m_clients)
			{
				if (client.clientId != participantClientId)
				{
					continue;
				}

				ApplyLevelUpsAfterXp(client, rewards.experience);
				if (rewards.gold != 0u)
				{
					std::string walletErr;
					if (!m_playerWallet.AddCurrency(client, kCurrencyGold, rewards.gold, walletErr))
					{
						LOG_WARN(Net,
							"[ServerApp] Dynamic event gold grant blocked (client_id={}, err={})",
							client.clientId,
							walletErr);
					}
				}
				for (const ItemStack& rewardItem : rewards.items)
				{
					AddItemToInventory(client, rewardItem);
				}
				if (!rewards.items.empty())
				{
					(void)SendInventoryDelta(client, rewards.items);
				}
				(void)SendDynamicEventState(
					client,
					eventState,
					eventState.definition.completionNotificationText,
					rewards.experience,
					rewards.gold,
					rewards.items);
				(void)SendWalletUpdate(client);
				SaveConnectedClient(client, "dynamic_event_reward");
				LOG_INFO(Net, "[ServerApp] Dynamic event rewards granted (event_id={}, client_id={}, xp={}, gold={}, items={})",
					eventState.definition.eventId,
					client.clientId,
					rewards.experience,
					rewards.gold,
					rewards.items.size());
				break;
			}
		}

		eventState.phaseMobEntityIds.clear();
		eventState.participantClientIds.clear();
		eventState.currentPhaseIndex = 0;
		eventState.currentPhaseProgress = 0;
		ScheduleDynamicEventTrigger(eventState, true);
		LOG_INFO(Net, "[ServerApp] Dynamic event completed (event_id={}, next_trigger_tick={})",
			eventState.definition.eventId,
			eventState.nextTriggerTick);
	}

	void ServerApp::AddDynamicEventParticipant(DynamicEventState& eventState, const ConnectedClient& client)
	{
		if (std::find(eventState.participantClientIds.begin(), eventState.participantClientIds.end(), client.clientId)
			!= eventState.participantClientIds.end())
		{
			return;
		}

		eventState.participantClientIds.push_back(client.clientId);
		LOG_INFO(Net, "[ServerApp] Dynamic event participant added (event_id={}, client_id={})",
			eventState.definition.eventId,
			client.clientId);
	}

	bool ServerApp::HasPlayersInZone(uint32_t zoneId) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (client.hasReplicatedState && client.zoneId == zoneId)
			{
				return true;
			}
		}

		return false;
	}

	void ServerApp::UpdateDynamicEvents()
	{
		if (m_dynamicEvents.empty())
		{
			return;
		}

		for (DynamicEventState& eventState : m_dynamicEvents)
		{
			if (eventState.status == DynamicEventStatus::Active)
			{
				for (size_t index = 0; index < eventState.phaseMobEntityIds.size();)
				{
					const EntityId entityId = eventState.phaseMobEntityIds[index];
					MobEntity* mob = FindMobByEntityId(entityId);
					if (mob == nullptr)
					{
						eventState.phaseMobEntityIds.erase(eventState.phaseMobEntityIds.begin() + static_cast<std::ptrdiff_t>(index));
						continue;
					}

					if (mob->pendingDespawn)
					{
						(void)DespawnDynamicEventMob(eventState, entityId, "phase_kill");
						continue;
					}

					++index;
				}

				const DynamicEventPhaseDefinition& phase = eventState.definition.phases[eventState.currentPhaseIndex];
				if (eventState.currentPhaseProgress >= phase.progressRequired && eventState.phaseMobEntityIds.empty())
				{
					(void)AdvanceDynamicEventPhase(eventState);
				}
				continue;
			}

			if (m_currentTick < eventState.nextTriggerTick)
			{
				continue;
			}

			if (!HasPlayersInZone(eventState.definition.zoneId))
			{
				LOG_DEBUG(Net, "[ServerApp] Dynamic event start delayed: no players in zone (event_id={}, zone_id={})",
					eventState.definition.eventId,
					eventState.definition.zoneId);
				continue;
			}

			if (!StartDynamicEvent(eventState))
			{
				ScheduleDynamicEventTrigger(eventState, true);
				LOG_WARN(Net, "[ServerApp] Dynamic event start rescheduled after failure (event_id={})",
					eventState.definition.eventId);
			}
		}
	}
}
