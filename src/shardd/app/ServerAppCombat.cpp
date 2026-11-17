// ServerApp — combat : attaques, dégâts aux monstres, réapparition et ramassage, butin, menace, déplacement des monstres, régénération des ressources.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	void ServerApp::HandleAttackRequest(const Endpoint& endpoint, uint32_t clientId, EntityId targetEntityId)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (!client->hasReplicatedState)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: attacker has no replicated state (client_id={})", client->clientId);
			return;
		}

		if ((client->stateFlags & kEntityStateDead) != 0u)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: attacker is dead (client_id={}, entity_id={})",
				client->clientId,
				client->entityId);
			return;
		}

		MobEntity* target = FindMobByEntityId(targetEntityId);
		if (target == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: invalid mob target (client_id={}, target_entity_id={})",
				client->clientId,
				targetEntityId);
			return;
		}
		// ETAPE3.2 (chantier 4.5) — cible (validée) gardée dans les snapshots malgré le plafond.
		client->lastTargetEntityId = targetEntityId;

		if (!engine::world::cells::SameMap(target->zoneId, client->zoneId))
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: cross-zone target (client_id={}, attacker_zone={}, target_zone={})",
				client->clientId,
				client->zoneId,
				target->zoneId);
			return;
		}

		if ((target->stateFlags & kEntityStateDead) != 0u || target->stats.currentHealth == 0)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: target already dead (client_id={}, target_entity_id={})",
				client->clientId,
				target->entityId);
			return;
		}

		// Monde vivant — les PNJ d'ambiance (villageois, gardes, marchands) ne
		// sont pas des cibles de combat, sauf pour une race qu'ils attaquent à vue
		// (Enfers : un humain peut riposter contre la Légion).
		if (!IsMobAttackableBy(*target, *client))
		{
			LOG_DEBUG(Net, "[ServerApp] AttackRequest ignored: non-attackable NPC (client_id={}, target_entity_id={})",
				client->clientId,
				target->entityId);
			return;
		}

		if (m_currentTick < client->combat.nextAttackTick)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: cooldown active (client_id={}, tick={}, next_attack_tick={})",
				client->clientId,
				m_currentTick,
				client->combat.nextAttackTick);
			return;
		}

		const float distanceSquared = DistanceSquaredXZ(
			client->positionMetersX,
			client->positionMetersZ,
			target->positionMetersX,
			target->positionMetersZ);
		const float attackRangeSquared = client->combat.attackRangeMeters * client->combat.attackRangeMeters;
		if (distanceSquared > attackRangeSquared)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: target out of range (client_id={}, target_entity_id={}, distance_sq={:.2f}, range_sq={:.2f})",
				client->clientId,
				target->entityId,
				distanceSquared,
				attackRangeSquared);
			return;
		}

		// Combat SP2 — le cooldown est consommé même sur un raté (le coup est parti).
		client->combat.nextAttackTick = m_currentTick + client->combat.cooldownTicks;
		const combat::AttackRollResult roll = combat::ResolveAttackRoll(
			client->combat.damagePerHit,
			client->combat.accuracy,
			client->combat.critRate,
			client->combat.critMult,
			NextCombatRoll01(),
			NextCombatRoll01());
		uint32_t eventFlags = 0u;
		if (roll.miss) eventFlags |= kCombatEventFlagMiss;
		if (roll.crit) eventFlags |= kCombatEventFlagCrit;
		if (roll.miss)
		{
			// Raté : aucun dégât, mais l'événement part quand même (combat log/HUD).
			CombatEventMessage missEvent{};
			missEvent.attackerEntityId = client->entityId;
			missEvent.targetEntityId = target->entityId;
			missEvent.damage = 0u;
			missEvent.targetCurrentHealth = target->stats.currentHealth;
			missEvent.targetMaxHealth = target->stats.maxHealth;
			missEvent.targetStateFlags = target->stateFlags;
			missEvent.flags = eventFlags;
			LOG_INFO(Net, "[ServerApp] Attack missed (attacker_entity_id={}, target_entity_id={})",
				missEvent.attackerEntityId,
				missEvent.targetEntityId);
			BroadcastCombatEvent(missEvent);
			return;
		}

		// Combat SP3 — modificateurs d'auras (buff de l'attaquant, debuff de la
		// cible) appliqués au jet, puis chemin commun dégâts/mort/loot/XP.
		const uint32_t modifiedDamage = ApplyAuraDamageModifiers(roll.damage, client->auras, target->auras);
		ApplyPlayerDamageToMob(*client, *target, modifiedDamage, eventFlags);
	}

	void ServerApp::ApplyPlayerDamageToMob(ConnectedClient& attacker, MobEntity& target, uint32_t rolledDamage, uint32_t eventFlags)
	{
		// Monde vivant — garde centrale (attaques, sorts, zones d'effet) : un PNJ
		// d'ambiance n'encaisse jamais de dégâts (sauf d'une race qu'il attaque à vue).
		if (!IsMobAttackableBy(target, attacker))
			return;
		const uint32_t appliedDamage = std::min(rolledDamage, target.stats.currentHealth);
		if (appliedDamage == 0)
		{
			LOG_DEBUG(Net, "[ServerApp] Damage ignored: zero after clamp (client_id={}, target_entity_id={})",
				attacker.clientId,
				target.entityId);
			return;
		}

		target.stats.currentHealth -= appliedDamage;
		if (target.isDynamicEventMob && target.owningEventIndex < m_dynamicEvents.size())
		{
			DynamicEventState& eventState = m_dynamicEvents[target.owningEventIndex];
			AddDynamicEventParticipant(eventState, attacker);
		}
		if (target.stats.currentHealth == 0)
		{
			target.stateFlags |= kEntityStateDead;
			target.pendingDespawn = true;
			target.aggroTargetEntityId = 0;
			target.threatTable.clear();
			// Combat SP3 — plus rien à ticker sur un mob mort.
			target.auras.clear();
			// Combat SP4 — effacement immédiat du threat meter côté clients.
			BroadcastThreatUpdate(target);
			if (target.isDynamicEventMob && target.owningEventIndex < m_dynamicEvents.size())
			{
				DynamicEventState& eventState = m_dynamicEvents[target.owningEventIndex];
				if (eventState.currentPhaseIndex < eventState.definition.phases.size())
				{
					++eventState.currentPhaseProgress;
					const DynamicEventPhaseDefinition& phase = eventState.definition.phases[eventState.currentPhaseIndex];
					LOG_INFO(Net, "[ServerApp] Dynamic event progress updated (event_id={}, phase_id={}, progress={}/{})",
						eventState.definition.eventId,
						phase.phaseId,
						eventState.currentPhaseProgress,
						phase.progressRequired);
					BroadcastDynamicEventState(eventState, phase.notificationText, 0, 0, 0, {});
				}
			}
			LOG_INFO(Net, "[ServerApp] Mob died (entity_id={}, attacker_entity_id={})", target.entityId, attacker.entityId);
			if (!target.hasSpawnedLoot)
			{
				target.hasSpawnedLoot = true;
				// M32.2 — Resolve loot owner based on party loot mode.
				// Validation v12 — butin AUTOMATIQUE : crédit direct du looter
				// (fenêtre de butin côté client) au lieu d'un sac à ramasser.
				const EntityId looterEntityId = ResolvePartyLooterEntityId(attacker);
				AutoLootMobToKiller(target, looterEntityId != 0 ? looterEntityId : attacker.entityId);
			}
			// M32.2 — Distribute XP to party members in range.
			// Combat SP1 — XP data-driven par archétype (fallback constante MVP
			// pour un mob dont l'archétype ne définit pas de récompense).
			DistributePartyXp(attacker,
			    target.positionMetersX,
			    target.positionMetersZ,
			    target.xpReward != 0u ? target.xpReward : kBaseXpPerMobKill);
			ApplyQuestEvent(attacker, QuestStepType::Kill, std::string("mob:") + std::to_string(target.archetypeId), 1, "kill");
		}

		CombatEventMessage combatEvent{};
		combatEvent.attackerEntityId = attacker.entityId;
		combatEvent.targetEntityId = target.entityId;
		combatEvent.damage = appliedDamage;
		combatEvent.targetCurrentHealth = target.stats.currentHealth;
		combatEvent.targetMaxHealth = target.stats.maxHealth;
		combatEvent.targetStateFlags = target.stateFlags;
		// Combat SP2 — bit critique éventuel (le raté est parti plus haut).
		combatEvent.flags = eventFlags;
		LOG_INFO(Net,
			"[ServerApp] Attack applied (attacker_entity_id={}, target_entity_id={}, damage={}, hp={}/{})",
			combatEvent.attackerEntityId,
			combatEvent.targetEntityId,
			combatEvent.damage,
			combatEvent.targetCurrentHealth,
			combatEvent.targetMaxHealth);
		BroadcastCombatEvent(combatEvent);
	}

	void ServerApp::HandleRespawnRequest(const Endpoint& endpoint, uint32_t clientId, uint8_t destination)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] RespawnRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] RespawnRequest ignored: client_id mismatch (expected={}, got={})",
				client->clientId,
				clientId);
			return;
		}

		if ((client->stateFlags & kEntityStateDead) == 0u)
		{
			LOG_WARN(Net, "[ServerApp] RespawnRequest ignored: player is alive (client_id={})", client->clientId);
			return;
		}

		// Wire v13 — point de réapparition du type demandé (cimetière/auberge)
		// LE PLUS PROCHE du lieu de mort ; repli : point d'entrée en monde.
		float respawnX = client->spawnPositionMetersX;
		float respawnY = client->spawnPositionMetersY;
		float respawnZ = client->spawnPositionMetersZ;
		const std::string* respawnGraveyardId = nullptr; // lore v1.0.114 — cimetière choisi (gy=)
		{
			float bestDistSq = std::numeric_limits<float>::max();
			bool found = false;
			for (const RespawnPointDefinition& point : m_respawnPoints)
			{
				if (!engine::world::cells::SameMap(point.zoneId, client->zoneId) || point.destinationType != destination)
					continue;
				const float dxr = point.positionMetersX - client->positionMetersX;
				const float dzr = point.positionMetersZ - client->positionMetersZ;
				const float distSqR = dxr * dxr + dzr * dzr;
				// Cimetière : filtre faction (rayon neutre). Auberge : aucune restriction.
				if (destination == kRespawnDestinationGraveyard)
				{
					const float dist = std::sqrt(distSqR);
					if (!engine::world::IsGraveyardEligibleForRespawn(
							dist, point.neutralRadiusM, point.ownerFactionId, client->factionId))
					{
						continue;
					}
				}
				if (distSqR < bestDistSq)
				{
					bestDistSq = distSqR;
					respawnX = point.positionMetersX;
					respawnY = point.positionMetersY;
					respawnZ = point.positionMetersZ;
					respawnGraveyardId = &point.graveyardId;
					found = true;
				}
			}
			if (!found)
			{
				LOG_INFO(Net, "[ServerApp] Respawn fallback to enter-world spawn (client_id={}, destination={}, zone_id={})",
					client->clientId, destination, client->zoneId);
			}
		}
		client->positionMetersX = respawnX;
		client->positionMetersY = respawnY;
		client->positionMetersZ = respawnZ;
		client->velocityMetersPerSecondX = 0.0f;
		client->velocityMetersPerSecondY = 0.0f;
		client->velocityMetersPerSecondZ = 0.0f;
		client->stats.currentHealth = client->stats.maxHealth;
		// Combat SP3 — la ressource revient aussi à plein au respawn.
		client->currentResource = client->maxResource;
		client->stateFlags &= ~kEntityStateDead;
		// Sécurité : plus aucune menace résiduelle ne doit pointer sur le ressuscité
		// (déjà purgée à la mort, mais un mob a pu être spawné entre-temps).
		PurgeThreatForEntity(client->entityId);
		// Correction SP1 — (1) reset de l'état anti-triche : sans ça, le premier
		// Input émis depuis le spawn serait vu comme un TeleportHack (la trace
		// anti-triche pointe encore le lieu de mort) ; (2) position IMPOSÉE au
		// client : le mouvement étant client-autoritaire, sans ce message le
		// client écraserait la téléportation au tick suivant (bug SP2 corrigé).
		m_antiCheat.Reset(client->persistenceCharacterKey);
		SendForcePosition(*client, kForcePositionReasonRespawn);
		// Validation v12 — événement de résurrection : les snapshots excluent
		// l'entité du joueur lui-même, ses PV/flags ne sont rafraîchis que par
		// les CombatEvent qui le ciblent. Sans cet événement, le client garde
		// le flag mort → l'écran de mort ne se fermait JAMAIS après respawn.
		CombatEventMessage resurrectionEvent{};
		resurrectionEvent.attackerEntityId = 0;
		resurrectionEvent.targetEntityId = client->entityId;
		resurrectionEvent.damage = 0;
		resurrectionEvent.targetCurrentHealth = client->stats.currentHealth;
		resurrectionEvent.targetMaxHealth = client->stats.maxHealth;
		resurrectionEvent.targetStateFlags = client->stateFlags;
		resurrectionEvent.flags = kCombatEventFlagResurrection;
		BroadcastCombatEvent(resurrectionEvent);
		UpdateClientInterest(*client);
		// Lore v1.0.114 — réapparaître à un cimetière le compte comme visité (avant la
		// sauvegarde ci-dessous, qui l'écrit).
		if (respawnGraveyardId != nullptr && !respawnGraveyardId->empty()
			&& engine::world::cells::CellMapOfZoneId(client->zoneId) == engine::world::cells::CellMap::Royaumes)
		{
			DiscoveryEvents events;
			if (client->discovery.RecordGraveyardVisit(m_discoveryCatalog, *respawnGraveyardId, events))
				SendDiscoveryEvents(*client, events);
		}
		SaveConnectedClient(*client, "respawn");
		LOG_INFO(Net,
			"[ServerApp] Player respawned (client_id={}, entity_id={}, pos=({:.1f}, {:.1f}, {:.1f}), hp={}/{})",
			client->clientId,
			client->entityId,
			client->positionMetersX,
			client->positionMetersY,
			client->positionMetersZ,
			client->stats.currentHealth,
			client->stats.maxHealth);
	}

	void ServerApp::SendForcePosition(ConnectedClient& client, uint8_t reason)
	{
		// ETAPE3.3 — position imposée : l'état de mouvement officiel repart d'ici, dans
		// une nouvelle époque (les commandes d'avant le téléport seront ignorées).
		ResetClientMovement(client);
		client.lastForcePositionReason = reason;
		EmitForcePosition(client, reason);
	}

	void ServerApp::EmitForcePosition(ConnectedClient& client, uint8_t reason)
	{
		// v1.0.125 — à bord, la position imposée est celle sur le pont (repère du bateau).
		if (client.boatIndex >= 0)
		{
			EmitBoatAboard(client);
			return;
		}
		ForcePositionMessage message{};
		message.clientId = client.clientId;
		message.moveEpoch = client.moveEpoch;
		message.positionX = client.positionMetersX;
		message.positionY = client.positionMetersY;
		message.positionZ = client.positionMetersZ;
		message.yawRadians = client.yawRadians;
		message.reason = reason;
		if (!SendDatagram(client.endpoint, EncodeForcePosition(message)))
		{
			LOG_WARN(Net, "[ServerApp] ForcePosition send failed (client_id={}, reason={})",
				client.clientId, reason);
			return;
		}
		client.lastForcePositionSentMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		LOG_INFO(Net, "[ServerApp] ForcePosition sent (client_id={}, pos=({:.1f},{:.1f},{:.1f}), reason={})",
			client.clientId, message.positionX, message.positionY, message.positionZ, reason);
	}

	void ServerApp::HandlePickupRequest(const Endpoint& endpoint, uint32_t clientId, EntityId lootBagEntityId)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		LootBagEntity* lootBag = FindLootBagByEntityId(lootBagEntityId);
		if (lootBag == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: invalid loot bag (client_id={}, loot_bag_entity_id={})",
				client->clientId,
				lootBagEntityId);
			return;
		}

		if (!engine::world::cells::SameMap(lootBag->zoneId, client->zoneId))
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: cross-zone bag (client_id={}, client_zone={}, bag_zone={})",
				client->clientId,
				client->zoneId,
				lootBag->zoneId);
			return;
		}

		if (lootBag->visibility == LootVisibility::Owner && lootBag->ownerEntityId != client->entityId)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: ownership mismatch (client_id={}, loot_bag_entity_id={}, owner_entity_id={})",
				client->clientId,
				lootBag->entityId,
				lootBag->ownerEntityId);
			return;
		}

		const float pickupRangeMeters = static_cast<float>(m_config.GetDouble("server.loot_pickup_range_meters", kDefaultLootPickupRangeMeters));
		const float distanceSquared = DistanceSquaredXZ(
			client->positionMetersX,
			client->positionMetersZ,
			lootBag->positionMetersX,
			lootBag->positionMetersZ);
		const float pickupRangeSquared = pickupRangeMeters * pickupRangeMeters;
		if (distanceSquared > pickupRangeSquared)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: loot bag out of range (client_id={}, loot_bag_entity_id={}, distance_sq={:.2f}, range_sq={:.2f})",
				client->clientId,
				lootBag->entityId,
				distanceSquared,
				pickupRangeSquared);
			return;
		}

		if (lootBag->items.empty())
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: empty loot bag (client_id={}, loot_bag_entity_id={})",
				client->clientId,
				lootBag->entityId);
			return;
		}

		const std::vector<ItemStack> pickedItems = lootBag->items;
		for (const ItemStack& item : pickedItems)
		{
			AddItemToInventory(*client, item);
		}

		if (CellGrid* zoneGrid = GetOrCreateZoneGrid(lootBag->zoneId); zoneGrid != nullptr)
		{
			(void)zoneGrid->RemoveEntity(lootBag->entityId);
		}
		m_lootBags.erase(
			std::remove_if(
				m_lootBags.begin(),
				m_lootBags.end(),
				[lootBagEntityId](const LootBagEntity& lootBagEntry)
				{
					return lootBagEntry.entityId == lootBagEntityId;
				}),
			m_lootBags.end());

		LOG_INFO(Net, "[ServerApp] Loot bag picked up (client_id={}, loot_bag_entity_id={}, item_count={})",
			client->clientId,
			lootBagEntityId,
			pickedItems.size());
		for (const ItemStack& item : pickedItems)
		{
			ApplyQuestEvent(*client, QuestStepType::Collect, std::string("item:") + std::to_string(item.itemId), item.quantity, "pickup");
		}
		SaveConnectedClient(*client, "pickup");
		(void)SendInventoryDelta(*client, pickedItems);
	}

	/// Loot réel (2026-07-19) — Tire le drop effectif d'une entrée : proba
	/// dropChancePct (m_combatRng), puis quantité uniforme [minCount,
	/// maxCount] si bornée, sinon quantité fixe item.quantity.
	/// \return true si l'entrée droppe (outItem rempli), false si le tirage
	/// échoue. Effet de bord : consomme m_combatRng.
	bool ServerApp::RollLootEntry(const LootTableEntry& entry, ItemStack& outItem)
	{
		if (entry.dropChancePct < 100u)
		{
			std::uniform_int_distribution<uint32_t> chanceDist(1u, 100u);
			if (chanceDist(m_combatRng) > entry.dropChancePct)
			{
				return false;
			}
		}
		outItem = entry.item;
		if (entry.maxCount > 0u)
		{
			std::uniform_int_distribution<uint32_t> countDist(entry.minCount, entry.maxCount);
			outItem.quantity = countDist(m_combatRng);
		}
		return true;
	}

	void ServerApp::AutoLootMobToKiller(const MobEntity& mob, EntityId looterEntityId)
	{
		// Même collecte que SpawnLootBagForMob : toutes les entrées de la table
		// pour cet archétype (la visibilité owner/public ne concerne que les
		// sacs ; en crédit direct le looter du groupe a déjà été résolu).
		// Loot réel (2026-07-19) : chaque entrée passe par RollLootEntry
		// (proba de drop + quantité aléatoire).
		std::vector<ItemStack> droppedItems;
		for (const LootTableEntry& entry : m_lootTableEntries)
		{
			ItemStack rolled{};
			if (entry.sourceArchetypeId == mob.archetypeId && RollLootEntry(entry, rolled))
			{
				droppedItems.push_back(rolled);
			}
		}
		if (droppedItems.empty())
		{
			// Pas de loot pour cet archétype (ou tirages malchanceux) : pas de
			// fenêtre côté client.
			LOG_DEBUG(Net, "[ServerApp] Auto-loot skipped: no loot table entry (mob_entity_id={}, archetype_id={})",
				mob.entityId, mob.archetypeId);
			return;
		}

		ConnectedClient* looter = FindClientByEntityId(looterEntityId);
		if (looter == nullptr)
		{
			// Looter déconnecté entre le coup fatal et la mort : on retombe sur
			// le sac au sol historique (ramassable plus tard, touche F).
			LOG_WARN(Net, "[ServerApp] Auto-loot fallback to bag: looter not connected (looter_entity_id={})",
				looterEntityId);
			SpawnLootBagForMob(mob, looterEntityId);
			return;
		}

		for (const ItemStack& item : droppedItems)
		{
			AddItemToInventory(*looter, item);
			ApplyQuestEvent(*looter, QuestStepType::Collect,
				std::string("item:") + std::to_string(item.itemId), item.quantity, "auto_loot");
		}
		SaveConnectedClient(*looter, "auto_loot");
		(void)SendInventoryDelta(*looter, droppedItems);

		LootNotifyMessage lootNotify{};
		lootNotify.clientId = looter->clientId;
		lootNotify.items = droppedItems;
		if (!SendDatagram(looter->endpoint, EncodeLootNotify(lootNotify)))
		{
			LOG_WARN(Net, "[ServerApp] LootNotify send failed (client_id={})", looter->clientId);
		}
		LOG_INFO(Net, "[ServerApp] Auto-loot credited (client_id={}, mob_entity_id={}, item_count={})",
			looter->clientId, mob.entityId, droppedItems.size());
	}

	void ServerApp::SpawnLootBagForMob(const MobEntity& mob, EntityId ownerEntityId)
	{
		std::vector<ItemStack> droppedItems;
		LootVisibility visibility = LootVisibility::Owner;
		bool foundLoot = false;
		for (const LootTableEntry& entry : m_lootTableEntries)
		{
			if (entry.sourceArchetypeId != mob.archetypeId)
			{
				continue;
			}

			if (!foundLoot)
			{
				visibility = entry.visibility;
				foundLoot = true;
			}
			else if (visibility != entry.visibility)
			{
				LOG_WARN(Net, "[ServerApp] Loot bag spawn ignored mixed visibility entries (mob_entity_id={}, archetype_id={})",
					mob.entityId,
					mob.archetypeId);
				return;
			}

			// Loot réel (2026-07-19) — tirage proba + quantité (la visibilité
			// est validée sur TOUTES les entrées de l'archétype, tirées ou non).
			ItemStack rolled{};
			if (RollLootEntry(entry, rolled))
			{
				droppedItems.push_back(rolled);
			}
		}

		if (!foundLoot)
		{
			LOG_WARN(Net, "[ServerApp] Loot bag spawn skipped: no loot table entry (mob_entity_id={}, archetype_id={})",
				mob.entityId,
				mob.archetypeId);
			return;
		}
		if (droppedItems.empty())
		{
			// Tirages malchanceux : rien à mettre dans le sac, pas de spawn.
			LOG_DEBUG(Net, "[ServerApp] Loot bag spawn skipped: rolls empty (mob_entity_id={}, archetype_id={})",
				mob.entityId, mob.archetypeId);
			return;
		}

		CellGrid* zoneGrid = GetOrCreateZoneGrid(mob.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Loot bag spawn FAILED: zone grid unavailable (mob_entity_id={}, zone_id={})",
				mob.entityId,
				mob.zoneId);
			return;
		}

		LootBagEntity lootBag{};
		lootBag.entityId = m_nextServerEntityId++;
		lootBag.zoneId = mob.zoneId;
		lootBag.archetypeId = kDefaultLootBagArchetypeId;
		lootBag.positionMetersX = mob.positionMetersX;
		lootBag.positionMetersY = mob.positionMetersY;
		lootBag.positionMetersZ = mob.positionMetersZ;
		lootBag.visibility = visibility;
		lootBag.ownerEntityId = visibility == LootVisibility::Owner ? ownerEntityId : 0;
		lootBag.items = droppedItems;

		CellCoord cell{};
		if (!zoneGrid->UpsertEntity(lootBag.entityId, lootBag.positionMetersX, lootBag.positionMetersZ, cell))
		{
			LOG_ERROR(Net, "[ServerApp] Loot bag spawn FAILED: spatial insert failed (loot_bag_entity_id={})", lootBag.entityId);
			return;
		}

		m_lootBags.push_back(lootBag);
		LOG_INFO(Net, "[ServerApp] Loot bag spawned (loot_bag_entity_id={}, mob_entity_id={}, owner_entity_id={}, item_count={}, visibility={})",
			lootBag.entityId,
			mob.entityId,
			lootBag.ownerEntityId,
			lootBag.items.size(),
			lootBag.visibility == LootVisibility::Owner ? "owner" : "public");
	}

	void ServerApp::UpdateThreatFromCombatEvent(const CombatEventMessage& message)
	{
		MobEntity* targetMob = FindMobByEntityId(message.targetEntityId);
		if (targetMob == nullptr || message.damage == 0 || (targetMob->stateFlags & kEntityStateDead) != 0u)
		{
			return;
		}

		const ConnectedClient* attacker = FindClientByEntityId(message.attackerEntityId);
		if (attacker == nullptr)
		{
			return;
		}

		for (ThreatEntry& entry : targetMob->threatTable)
		{
			if (entry.entityId == attacker->entityId)
			{
				entry.threat += message.damage;
				LOG_INFO(Net, "[ServerApp] Threat updated (mob_entity_id={}, attacker_entity_id={}, threat={})",
					targetMob->entityId,
					attacker->entityId,
					entry.threat);
				RefreshMobAggroTarget(*targetMob);
				if (targetMob->aggroTargetEntityId != 0)
				{
					SetMobAiState(*targetMob, MobAiState::Aggro);
				}
				return;
			}
		}

		ThreatEntry entry{};
		entry.entityId = attacker->entityId;
		entry.threat = message.damage;
		targetMob->threatTable.push_back(entry);
		LOG_INFO(Net, "[ServerApp] Threat added (mob_entity_id={}, attacker_entity_id={}, threat={})",
			targetMob->entityId,
			attacker->entityId,
			entry.threat);
		RefreshMobAggroTarget(*targetMob);
		if (targetMob->aggroTargetEntityId != 0)
		{
			SetMobAiState(*targetMob, MobAiState::Aggro);
		}
	}

	void ServerApp::RefreshMobAggroTarget(MobEntity& mob)
	{
		mob.threatTable.erase(
			std::remove_if(
				mob.threatTable.begin(),
				mob.threatTable.end(),
				[this, &mob](const ThreatEntry& entry)
				{
					const ConnectedClient* target = FindClientByEntityId(entry.entityId);
					return target == nullptr
						|| !target->hasReplicatedState
						|| !engine::world::cells::SameMap(target->zoneId, mob.zoneId)
						|| target->stats.currentHealth == 0
						|| (target->stateFlags & kEntityStateDead) != 0u
						|| target->gm.invisible // ADM.3 — un MJ invisible n'est jamais ciblé
						|| entry.threat == 0;
				}),
			mob.threatTable.end());

		EntityId bestEntityId = 0;
		uint32_t bestThreat = 0;
		for (const ThreatEntry& entry : mob.threatTable)
		{
			if (entry.threat > bestThreat)
			{
				bestThreat = entry.threat;
				bestEntityId = entry.entityId;
			}
		}

		if (mob.aggroTargetEntityId != bestEntityId)
		{
			LOG_INFO(Net, "[ServerApp] Mob target updated (mob_entity_id={}, target_entity_id={}, threat={})",
				mob.entityId,
				bestEntityId,
				bestThreat);
			mob.aggroTargetEntityId = bestEntityId;
		}
	}

	bool ServerApp::MoveMobTowards(MobEntity& mob, float targetPositionX, float targetPositionZ)
	{
		const float dx = targetPositionX - mob.positionMetersX;
		const float dz = targetPositionZ - mob.positionMetersZ;
		const float distanceSquared = (dx * dx) + (dz * dz);
		if (distanceSquared <= 0.0001f)
		{
			mob.velocityMetersPerSecondX = 0.0f;
			mob.velocityMetersPerSecondZ = 0.0f;
			return true;
		}

		// ETAPE4.1 — durée couverte par la décision d'IA en cours (fréquence de la
		// classe du mob, rattrapage du temps écoulé) : la vitesse au sol ne dépend pas
		// de la fréquence de traitement.
		const float simulationDt = std::max(mob.aiStepSeconds, 1.0f / static_cast<float>(std::max<int64_t>(m_tickHz, 1)));
		// Combat SP3 — ralentissements actifs (SlowMobPercent cumulés, plancher
		// 10 % de la vitesse de base ; la valeur de base n'est jamais écrasée).
		const float slowFactor = std::clamp(
			1.0f - SumAuraPercent(mob.auras, SpellEffectType::SlowMobPercent) / 100.0f, 0.1f, 1.0f);
		const float maxStep = mob.moveSpeedMetersPerSecond * slowFactor * simulationDt;
		const float distance = std::sqrt(distanceSquared);
		if (distance <= maxStep)
		{
			mob.velocityMetersPerSecondX = dx / simulationDt;
			mob.velocityMetersPerSecondZ = dz / simulationDt;
			mob.positionMetersX = targetPositionX;
			mob.positionMetersZ = targetPositionZ;
			// Validation v12 — pas de mise à jour du yaw sur un micro-pas final
			// (< 15 cm) : combiné aux corrections de séparation, le mob « vibrait »
			// sur lui-même en recalculant son orientation sur des restes de pas.
			if (distance > 0.15f)
			{
				mob.yawRadians = std::atan2(mob.velocityMetersPerSecondX, mob.velocityMetersPerSecondZ);
			}
			return UpdateMobSpatialState(mob); // arrivé (si la grille accepte la position)
		}

		const float stepScale = maxStep / distance;
		mob.velocityMetersPerSecondX = (dx * stepScale) / simulationDt;
		mob.velocityMetersPerSecondZ = (dz * stepScale) / simulationDt;
		mob.positionMetersX += dx * stepScale;
		mob.positionMetersZ += dz * stepScale;
		mob.yawRadians = std::atan2(mob.velocityMetersPerSecondX, mob.velocityMetersPerSecondZ);
		// ETAPE4.1 — pas intermédiaire : PAS arrivé. Avant, le résultat de la grille
		// (presque toujours vrai) était renvoyé tel quel : une errance s'arrêtait après
		// un seul pas, un retour au point d'attache repassait au repos (et pouvait
		// s'endormir) loin de chez lui.
		(void)UpdateMobSpatialState(mob);
		return false;
	}

	bool ServerApp::UpdateMobSpatialState(MobEntity& mob)
	{
		// Validation v12 — séparation des mobs : plusieurs mobs poursuivant la
		// même cible convergent vers le même point et se SUPERPOSENT (constat
		// validation : deux sangliers fusionnés visuellement). On repousse ce
		// mob hors du rayon personnel des autres mobs vivants de la zone
		// (O(n²) acceptable : poignée de mobs par zone à ce stade).
		constexpr float kMobSeparationMeters = 1.2f;
		for (const MobEntity& other : m_mobs)
		{
			if (other.entityId == mob.entityId || !engine::world::cells::SameMap(other.zoneId, mob.zoneId))
				continue;
			if ((other.stateFlags & kEntityStateDead) != 0u)
				continue;
			const float sepDx = mob.positionMetersX - other.positionMetersX;
			const float sepDz = mob.positionMetersZ - other.positionMetersZ;
			const float sepDistSq = sepDx * sepDx + sepDz * sepDz;
			if (sepDistSq >= kMobSeparationMeters * kMobSeparationMeters)
				continue;
			if (sepDistSq <= 0.0001f)
			{
				// Exactement superposés : direction déterministe dérivée des ids.
				const float jitterAngle = static_cast<float>((mob.entityId % 8u)) * 0.7853981f;
				mob.positionMetersX += std::cos(jitterAngle) * kMobSeparationMeters;
				mob.positionMetersZ += std::sin(jitterAngle) * kMobSeparationMeters;
				continue;
			}
			const float sepDist = std::sqrt(sepDistSq);
			const float pushOut = (kMobSeparationMeters - sepDist);
			mob.positionMetersX += (sepDx / sepDist) * pushOut;
			mob.positionMetersZ += (sepDz / sepDist) * pushOut;
		}

		CellGrid* zoneGrid = GetOrCreateZoneGrid(mob.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Mob spatial update FAILED: zone grid unavailable (mob_entity_id={}, zone_id={})",
				mob.entityId,
				mob.zoneId);
			return false;
		}

		CellCoord cell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, cell))
		{
			LOG_WARN(Net, "[ServerApp] Mob spatial update skipped (mob_entity_id={}, pos=({:.2f}, {:.2f}))",
				mob.entityId,
				mob.positionMetersX,
				mob.positionMetersZ);
			return false;
		}

		LOG_DEBUG(Net, "[ServerApp] Mob spatial update OK (mob_entity_id={}, cell={}, {})",
			mob.entityId,
			cell.x,
			cell.z);
		return true;
	}

	void ServerApp::SetMobAiState(MobEntity& mob, MobAiState newState)
	{
		if (mob.aiState == newState)
		{
			return;
		}

		// FIX.6 — DEBUG : ~1 000 lignes en 6 min pour 53 mobs (Idle <-> Patrol).
		LOG_DEBUG(Net, "[ServerApp] Mob state changed (mob_entity_id={}, {} -> {})",
			mob.entityId,
			GetMobAiStateName(mob.aiState),
			GetMobAiStateName(newState));
		mob.aiState = newState;
	}

	void ServerApp::ResetMobThreat(MobEntity& mob)
	{
		const size_t clearedCount = mob.threatTable.size();
		mob.threatTable.clear();
		mob.aggroTargetEntityId = 0;
		// Combat SP4 — effacement immédiat du threat meter côté clients.
		if (clearedCount > 0)
		{
			BroadcastThreatUpdate(mob);
		}
		LOG_INFO(Net, "[ServerApp] Mob threat reset (mob_entity_id={}, cleared_entries={})",
			mob.entityId,
			clearedCount);
	}

	void ServerApp::PurgeThreatForEntity(EntityId entityId)
	{
		size_t purgedMobs = 0;
		for (MobEntity& mob : m_mobs)
		{
			const size_t before = mob.threatTable.size();
			mob.threatTable.erase(
				std::remove_if(mob.threatTable.begin(), mob.threatTable.end(),
					[entityId](const ThreatEntry& entry) { return entry.entityId == entityId; }),
				mob.threatTable.end());
			if (mob.aggroTargetEntityId == entityId)
			{
				mob.aggroTargetEntityId = 0;
			}
			if (mob.threatTable.size() != before)
			{
				++purgedMobs;
			}
		}
		if (purgedMobs > 0)
		{
			LOG_INFO(Net, "[ServerApp] Threat purged for entity (entity_id={}, mobs={})", entityId, purgedMobs);
		}
	}

	float ServerApp::NextCombatRoll01()
	{
		// distribution locale (stateless) : [0,1), borne haute exclue.
		std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
		return distribution(m_combatRng);
	}

	void ServerApp::RegenerateResources()
	{
		if (m_tickHz == 0)
		{
			return;
		}

		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		for (ConnectedClient& client : m_clients)
		{
			if (client.maxResource == 0 || client.currentResource >= client.maxResource)
			{
				continue;
			}
			// Pas de régénération pour un joueur mort (le respawn refill à plein).
			if ((client.stateFlags & kEntityStateDead) != 0u)
			{
				continue;
			}

			// 5 %/s hors combat, 2 %/s en combat (décision utilisateur SP3) ;
			// « en combat » = impliqué dans un CombatEvent depuis < 5 s.
			const bool inCombat = (nowMs - client.lastCombatInvolvementMs) < 5000u;
			const float ratePerSecond = inCombat ? 0.02f : 0.05f;
			client.resourceRegenCarry +=
				static_cast<float>(client.maxResource) * ratePerSecond / static_cast<float>(m_tickHz);
			if (client.resourceRegenCarry < 1.0f)
			{
				continue;
			}

			const uint32_t whole = static_cast<uint32_t>(client.resourceRegenCarry);
			client.resourceRegenCarry -= static_cast<float>(whole);
			client.currentResource = std::min(client.maxResource, client.currentResource + whole);
			// Push throttlé (≤ 1 / 500 ms) — le débit d'un cast force l'envoi, lui.
			SendResourceUpdate(client, false);
		}
	}

	void ServerApp::SendResourceUpdate(ConnectedClient& client, bool force)
	{
		const uint64_t nowMs = NowMonotonicMs();
		if (!force)
		{
			if ((nowMs - client.lastResourceUpdateSentMs) < 500u)
			{
				return;
			}
			if (client.currentResource == client.lastResourceUpdateSentValue)
			{
				return;
			}
		}

		ResourceUpdateMessage message{};
		message.clientId = client.clientId;
		message.currentResource = client.currentResource;
		message.maxResource = client.maxResource;
		if (!SendDatagram(client.endpoint, EncodeResourceUpdate(message)))
		{
			LOG_WARN(Net, "[ServerApp] ResourceUpdate send failed (client_id={})", client.clientId);
			return;
		}
		client.lastResourceUpdateSentMs = nowMs;
		client.lastResourceUpdateSentValue = client.currentResource;
	}
}
