// ServerApp — réplication : entités pertinentes, états d'entités, instantanés, événements de combat, messages au client, index des clients / mobs / sacs de butin.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	void ServerApp::RefreshReplication()
	{
		for (ConnectedClient& client : m_clients)
		{
			RefreshReplicationForClient(client);
		}
	}

	void ServerApp::RefreshReplicationForClient(ConnectedClient& client)
	{
		if (!client.hasCell || !client.hasReplicatedState)
		{
			return;
		}

		if (CellGrid* zoneGrid = GetOrCreateZoneGrid(client.zoneId); zoneGrid != nullptr)
		{
			zoneGrid->GatherEntityIds(client.interestCells, client.interestEntityIds);
		}
		else
		{
			LOG_WARN(Net, "[ServerApp] Replication refresh skipped: zone grid unavailable (client_id={}, zone_id={})",
				client.clientId,
				client.zoneId);
			return;
		}

		BuildRelevantEntityIds(client, m_relevantEntityScratch);
		// ETAPE3.2 — plus de Spawn / Despawn séparés : le snapshot en delta porte les
		// apparitions (entité absente de la base, état complet) et les retraits.
		client.replicatedEntityIds = m_relevantEntityScratch;
	}

	void ServerApp::BuildRelevantEntityIds(const ConnectedClient& client, std::vector<EntityId>& outEntityIds) const
	{
		outEntityIds.clear();
		// TG.4 — collecte avec distance carrée pour pouvoir cap top-N (par distance au joueur)
		// si la liste explose. On factorise la position du sujet au moment où l'on identifie
		// son type (client / mob / loot bag) : évite un second lookup à l'application du cap.
		// Capacité 0 (défaut) = comportement historique (pas de cap).
		struct Candidate { EntityId entityId; float distSq; };
		// Perf : tampon réutilisé d'un appel à l'autre (appelé pour chaque client à
		// chaque rafraîchissement de la réplication) au lieu d'être réalloué. Le tick
		// est mono-thread et la fonction non réentrante.
		static thread_local std::vector<Candidate> candidates;
		candidates.clear();
		candidates.reserve(client.interestEntityIds.size());
		const float cx = client.positionMetersX;
		const float cy = client.positionMetersY;
		const float cz = client.positionMetersZ;
		auto AddCandidate = [&](EntityId id, float x, float y, float z) {
			const float dx = x - cx, dy = y - cy, dz = z - cz;
			candidates.push_back({id, dx*dx + dy*dy + dz*dz});
		};

		for (EntityId entityId : client.interestEntityIds)
		{
			if (entityId == client.entityId)
			{
				continue;
			}

			const ConnectedClient* subjectClient = FindClientByEntityId(entityId);
			if (subjectClient != nullptr)
			{
				if (!subjectClient->hasReplicatedState)
				{
					continue;
				}
				// ADM.3 — maître de jeu invisible (/invisible) : absent des snapshots des autres.
				if (subjectClient->gm.invisible)
				{
					continue;
				}
				AddCandidate(entityId,
					subjectClient->positionMetersX, subjectClient->positionMetersY, subjectClient->positionMetersZ);
				continue;
			}

			if (const MobEntity* mob = FindMobByEntityId(entityId); mob != nullptr)
			{
				AddCandidate(entityId, mob->positionMetersX, mob->positionMetersY, mob->positionMetersZ);
				continue;
			}

			const LootBagEntity* lootBag = FindLootBagByEntityId(entityId);
			if (lootBag != nullptr)
			{
				if (lootBag->visibility == LootVisibility::Public || lootBag->ownerEntityId == client.entityId)
				{
					AddCandidate(entityId, lootBag->positionMetersX, lootBag->positionMetersY, lootBag->positionMetersZ);
				}
			}
		}

		// TG.4 — cap top-N par distance (squared, monotone donc équivalent à la distance euclidienne).
		// Si configuré et dépassé, on partial_sort sur les N premiers puis on tronque : le
		// rayon de vue se réduit de lui-même là où il y a foule.
		// ETAPE3.2 (chantier 4.5) — les membres du groupe et la cible courante passent
		// avant tout le monde : jamais coupés par le plafond.
		if (m_viewCapEntities != 0u && candidates.size() > static_cast<size_t>(m_viewCapEntities))
		{
			std::vector<EntityId> priority;
			if (client.lastTargetEntityId != 0u)
				priority.push_back(client.lastTargetEntityId);
			if (client.castTargetEntityId != 0u)
				priority.push_back(client.castTargetEntityId);
			if (m_partySystem.IsInitialized())
			{
				if (const Party* party = m_partySystem.FindPartyByMember(client.clientId))
				{
					for (const PartyMember& member : party->members)
					{
						if (member.entityId != 0u)
							priority.push_back(member.entityId);
					}
				}
			}
			auto isPriority = [&priority](EntityId id) {
				return std::find(priority.begin(), priority.end(), id) != priority.end();
			};
			const size_t cap = static_cast<size_t>(m_viewCapEntities);
			std::partial_sort(candidates.begin(), candidates.begin() + cap, candidates.end(),
				[&isPriority](const Candidate& a, const Candidate& b) {
					const bool pa = isPriority(a.entityId);
					const bool pb = isPriority(b.entityId);
					if (pa != pb)
						return pa;
					return a.distSq < b.distSq;
				});
			candidates.resize(cap);
		}

		outEntityIds.reserve(candidates.size());
		for (const Candidate& c : candidates)
		{
			outEntityIds.push_back(c.entityId);
		}
	}

	EntityState ServerApp::BuildEntityState(const ConnectedClient& client) const
	{
		EntityState state{};
		state.positionX = client.positionMetersX;
		state.positionY = client.positionMetersY;
		state.positionZ = client.positionMetersZ;
		state.yawRadians = client.yawRadians;
		state.velocityX = client.velocityMetersPerSecondX;
		state.velocityY = client.velocityMetersPerSecondY;
		state.velocityZ = client.velocityMetersPerSecondZ;
		state.currentHealth = client.stats.currentHealth;
		state.maxHealth = client.stats.maxHealth;
		state.stateFlags = client.stateFlags;
		return state;
	}

	EntityState ServerApp::BuildEntityState(const MobEntity& mob) const
	{
		EntityState state{};
		state.positionX = mob.positionMetersX;
		state.positionY = mob.positionMetersY;
		state.positionZ = mob.positionMetersZ;
		state.yawRadians = mob.yawRadians;
		state.velocityX = mob.velocityMetersPerSecondX;
		state.velocityY = mob.velocityMetersPerSecondY;
		state.velocityZ = mob.velocityMetersPerSecondZ;
		state.currentHealth = mob.stats.currentHealth;
		state.maxHealth = mob.stats.maxHealth;
		state.stateFlags = mob.stateFlags;
		return state;
	}

	EntityState ServerApp::BuildEntityState(const LootBagEntity& lootBag) const
	{
		EntityState state{};
		state.positionX = lootBag.positionMetersX;
		state.positionY = lootBag.positionMetersY;
		state.positionZ = lootBag.positionMetersZ;
		state.yawRadians = lootBag.yawRadians;
		state.velocityX = lootBag.velocityMetersPerSecondX;
		state.velocityY = lootBag.velocityMetersPerSecondY;
		state.velocityZ = lootBag.velocityMetersPerSecondZ;
		state.currentHealth = 0;
		state.maxHealth = 0;
		state.stateFlags = lootBag.stateFlags;
		return state;
	}

	bool ServerApp::TryBuildSnapshotEntity(EntityId entityId, SnapshotEntity& outEntity) const
	{
		if (const ConnectedClient* client = FindClientByEntityId(entityId))
		{
			outEntity.entityId = client->entityId;
			outEntity.state = BuildEntityState(*client);
			// TD.4 : expose le clientId au snapshot pour que les autres clients puissent
			// afficher une plaque de nom au-dessus de cet avatar joueur.
			outEntity.playerClientId = client->clientId;
			// TD.5 : nom du perso (chargé depuis la DB au moment de l'admission) → plaque
			// avec le vrai nom au lieu du fallback "P<clientId>". Vide si la DB n'a pas pu
			// servir (Windows / DB indisponible), le client retombe sur "P<clientId>".
			outEntity.characterName = client->characterName;
			// TD.6 : genre du perso (migration 0067, "male"/"female") → le client sélectionne
			// le bon mesh skinné pour l'avatar distant. Vide = fallback "male" côté rendu.
			outEntity.gender = client->gender;
			// Personnalisation : race + apparence (migration 0097) → le client assemble
			// le corps de la race et les pièces choisies (visage, coiffure, cornes…).
			outEntity.raceId = client->raceId;
			outEntity.appearance = client->appearance;
			// TD.8 : état d'animation reporté par ce joueur → les autres clients jouent le bon
			// clip (emote/roll/run/sprint/jump/…) au lieu de dériver Idle/Walk de la vélocité.
			outEntity.animationState = static_cast<AvatarAnimState>(client->animationState);
			return true;
		}

		if (const MobEntity* mob = FindMobByEntityId(entityId))
		{
			outEntity.entityId = mob->entityId;
			outEntity.state = BuildEntityState(*mob);
			// playerClientId reste 0 par defaut (entite non-joueur, pas de plaque de nom).
			// Combat SP1 (wire v9) : l'archetypeId permet au client de rendre le mob
			// (mesh/nom/niveau/échelle résolus dans son CreatureCatalog).
			outEntity.archetypeId = mob->archetypeId;
			return true;
		}

		if (const LootBagEntity* lootBag = FindLootBagByEntityId(entityId))
		{
			outEntity.entityId = lootBag->entityId;
			outEntity.state = BuildEntityState(*lootBag);
			return true;
		}

		// Métiers SP1 — nodes de récolte : position statique, archetypeId dans la
		// plage réservée (base + typeId), flag dead = épuisé (label grisé client).
		if (m_gatheringSystem.IsInitialized())
		{
			if (const ResourceNodeRuntimeState* node = m_gatheringSystem.FindNode(entityId))
			{
				outEntity.entityId = node->entityId;
				outEntity.state.positionX = node->definition.positionMetersX;
				outEntity.state.positionY = node->definition.positionMetersY;
				outEntity.state.positionZ = node->definition.positionMetersZ;
				outEntity.state.currentHealth = node->available ? 1u : 0u;
				outEntity.state.maxHealth = 1u;
				outEntity.state.stateFlags = node->available ? 0u : kEntityStateDead;
				outEntity.archetypeId = kGatheringNodeArchetypeBase + node->definition.typeId;
				return true;
			}
		}

		// v1.0.125 — bateaux des lignes maritimes : pose du tick, archétype réservé, drapeau
		// « à quai » (embarquement possible côté client).
		if (const int32_t boat = FindBoatByEntityId(entityId); boat >= 0)
		{
			const BoatPose& p = m_boatPoses[static_cast<size_t>(boat)];
			outEntity.entityId = entityId;
			outEntity.state.positionX = static_cast<float>(p.x);
			outEntity.state.positionY = static_cast<float>(p.y);
			outEntity.state.positionZ = static_cast<float>(p.z);
			outEntity.state.yawRadians = p.yawRad;
			outEntity.state.velocityX = std::sin(p.yawRad) * p.speed;
			outEntity.state.velocityZ = std::cos(p.yawRad) * p.speed;
			outEntity.state.currentHealth = 1u;
			outEntity.state.maxHealth = 1u;
			outEntity.state.stateFlags = p.docked ? kEntityStateBoatDocked : 0u;
			outEntity.archetypeId = kBoatArchetypeId;
			return true;
		}

		LOG_WARN(Net, "[ServerApp] Snapshot build ignored: unknown entity_id={}", entityId);
		return false;
	}

	bool ServerApp::ShouldBroadcastCombatEventToClient(const ConnectedClient& receiver, EntityId attackerEntityId, EntityId targetEntityId) const
	{
		if (receiver.entityId == attackerEntityId || receiver.entityId == targetEntityId)
		{
			return true;
		}

		return ContainsEntityId(receiver.interestEntityIds, attackerEntityId)
			|| ContainsEntityId(receiver.interestEntityIds, targetEntityId);
	}

	void ServerApp::BroadcastCombatEvent(const CombatEventMessage& message)
	{
		UpdateThreatFromCombatEvent(message);

		// Combat SP3 — tamponne l'implication au combat des joueurs concernés
		// (la régénération de ressource passe en cadence « en combat » 5 s).
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		if (ConnectedClient* attacker = FindClientByEntityId(message.attackerEntityId))
		{
			attacker->lastCombatInvolvementMs = nowMs;
		}
		if (ConnectedClient* target = FindClientByEntityId(message.targetEntityId))
		{
			target->lastCombatInvolvementMs = nowMs;
		}

		size_t recipientCount = 0;
		for (const ConnectedClient& client : m_clients)
		{
			if (!ShouldBroadcastCombatEventToClient(client, message.attackerEntityId, message.targetEntityId))
			{
				continue;
			}

			++recipientCount;
			(void)SendCombatEvent(client, message);
		}

		LOG_INFO(Net,
			"[ServerApp] CombatEvent broadcast (attacker_entity_id={}, target_entity_id={}, damage={}, recipients={})",
			message.attackerEntityId,
			message.targetEntityId,
			message.damage,
			recipientCount);
	}

	bool ServerApp::SendZoneChange(const ConnectedClient& receiver, const ZoneChangeMessage& message)
	{
		const std::vector<std::byte> packet = EncodeZoneChange(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] ZoneChange send failed (client_id={}, zone_id={})",
				receiver.clientId,
				message.zoneId);
			return false;
		}

		LOG_INFO(Net,
			"[ServerApp] ZoneChange sent (client_id={}, zone_id={}, spawn=({:.2f}, {:.2f}, {:.2f}))",
			receiver.clientId,
			message.zoneId,
			message.spawnPositionX,
			message.spawnPositionY,
			message.spawnPositionZ);
		return true;
	}

	bool ServerApp::SendCombatEvent(const ConnectedClient& receiver, const CombatEventMessage& message)
	{
		const std::vector<std::byte> packet = EncodeCombatEvent(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net,
				"[ServerApp] CombatEvent send failed (client_id={}, attacker_entity_id={}, target_entity_id={})",
				receiver.clientId,
				message.attackerEntityId,
				message.targetEntityId);
			return false;
		}

		LOG_DEBUG(Net,
			"[ServerApp] CombatEvent sent (client_id={}, attacker_entity_id={}, target_entity_id={}, damage={})",
			receiver.clientId,
			message.attackerEntityId,
			message.targetEntityId,
			message.damage);
		return true;
	}

	bool ServerApp::SendChatRelay(const ConnectedClient& receiver, const ChatRelayMessage& message)
	{
		const std::vector<std::byte> packet = EncodeChatRelay(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net,
				"[ServerApp] ChatRelay send FAILED (receiver_client_id={}, sender_entity_id={})",
				receiver.clientId,
				message.senderEntityId);
			return false;
		}

		LOG_DEBUG(Net,
			"[ServerApp] ChatRelay sent (receiver_client_id={}, sender_entity_id={}, channel_wire={})",
			receiver.clientId,
			message.senderEntityId,
			message.channel);
		return true;
	}

	bool ServerApp::SendEmoteRelay(const ConnectedClient& receiver, const EmoteRelayMessage& message)
	{
		const std::vector<std::byte> packet = EncodeEmoteRelay(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net,
				"[ServerApp] EmoteRelay send FAILED (receiver_client_id={}, actor_entity_id={})",
				receiver.clientId,
				message.actorEntityId);
			return false;
		}

		LOG_DEBUG(Net,
			"[ServerApp] EmoteRelay sent (receiver_client_id={}, actor_entity_id={}, emote_id={})",
			receiver.clientId,
			message.actorEntityId,
			message.emoteId);
		return true;
	}

	bool ServerApp::SendWelcome(const ConnectedClient& client)
	{
		WelcomeMessage welcome{};
		welcome.clientId = client.clientId;
		welcome.tickHz = m_tickHz;
		welcome.snapshotHz = m_snapshotHz;
		const std::vector<std::byte> packet = EncodeWelcome(welcome);
		if (!SendDatagram(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Welcome send failed (client_id={}, endpoint={})",
				client.clientId,
				UdpTransport::EndpointToString(client.endpoint));
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Welcome sent (client_id={}, endpoint={}, tick_hz={}, snapshot_hz={})",
			client.clientId,
			UdpTransport::EndpointToString(client.endpoint),
			m_tickHz,
			m_snapshotHz);
		return true;
	}

	bool ServerApp::SendPlayerStats(const ConnectedClient& client)
	{
		if (!m_statsTables) return false;
		if (client.factionId.empty() || client.classId.empty()) return false;
		const engine::server::gameplay::Sex sex =
			(client.gender == "female") ? engine::server::gameplay::Sex::Female
			                            : engine::server::gameplay::Sex::Male;
		auto d = engine::server::gameplay::ComputeStats(
			*m_statsTables, client.factionId, client.classId, sex, client.level);
		if (!d) return false;
		// Chantier 2 SP-A — bonus d'équipement (source autoritaire : catalogue serveur).
		ApplyEquipmentBonus(client, *d);
		// Roadmap-2 (2026-07-19) — reflète aussi les auras de stats sur le wire.
		ApplyAuraStatModifiers(client.auras, *d);

		PlayerStatsMessage msg{};
		msg.clientId    = client.clientId;
		msg.maxHealth   = d->hp;
		msg.resource    = d->resource;
		msg.stamina     = d->stamina;
		msg.damage      = d->damage;
		msg.accuracy    = d->accuracy;
		msg.range       = d->range;
		msg.critRate    = d->critRate;
		msg.critMult    = d->critMult;
		msg.speedWalk   = d->speedWalk;
		msg.speedRun    = d->speedRun;
		msg.speedSprint = d->speedSprint;
		msg.perception  = d->perception;
		msg.stealth     = d->stealth;
		msg.resourceKey = d->resourceKey;
		// Combat SP3 (wire v11) — le client résout son kit de sorts via le profil.
		msg.profileId = client.profileId;

		const std::vector<std::byte> packet = EncodePlayerStats(msg);
		if (!SendDatagram(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendPlayerStats failed (client_id={})", client.clientId);
			return false;
		}
		LOG_INFO(Net, "[ServerApp] PlayerStats sent (client_id={}, faction={}, class={}, level={}, maxHp={})",
			client.clientId, client.factionId, client.classId, client.level, d->hp);
		return true;
	}

	// PR-C — pousse la progression de niveau (barre d'XP client). xpForNextLevel
	// via la MÊME courbe que ApplyXpGain (formulas::XpToNextLevel) pour cohérence :
	// 0 signale le cap de niveau. Appelé à l'enter-world et à chaque gain d'XP.
	bool ServerApp::SendPlayerXpUpdate(const ConnectedClient& client)
	{
		if (!m_statsTables) return false;
		PlayerXpUpdateMessage msg{};
		msg.clientId    = client.clientId;
		msg.level       = client.level;
		msg.xpIntoLevel = client.experiencePoints;
		const uint8_t levelU8 = static_cast<uint8_t>(std::min<uint32_t>(client.level, 255u));
		const uint8_t levelMaxU8 = static_cast<uint8_t>(std::min<uint32_t>(m_statsTables->levelMax, 255u));
		msg.xpForNextLevel = engine::server::formulas::XpToNextLevel(
			levelU8, m_statsTables->xpBase, m_statsTables->xpFactor, levelMaxU8);

		const std::vector<std::byte> packet = EncodePlayerXpUpdate(msg);
		if (!SendDatagram(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendPlayerXpUpdate failed (client_id={})", client.clientId);
			return false;
		}
		LOG_INFO(Net, "[ServerApp] PlayerXpUpdate sent (client_id={}, level={}, xp={}/{})",
			client.clientId, msg.level, msg.xpIntoLevel, msg.xpForNextLevel);
		return true;
	}

	bool ServerApp::SendSnapshot(ConnectedClient& client)
	{
		// ETAPE3.2 — état quantifié de chaque entité répliquée ; SnapshotSender choisit la
		// base (dernier snapshot acquitté par le client), n'écrit que ce qui a changé,
		// numérote et découpe en morceaux (~1100 o de charge utile).
		std::vector<engine::network::snapshot::WireEntity> current;
		current.reserve(client.replicatedEntityIds.size());
		for (EntityId entityId : client.replicatedEntityIds)
		{
			SnapshotEntity snapshotEntity{};
			if (!TryBuildSnapshotEntity(entityId, snapshotEntity))
			{
				continue;
			}
			engine::network::snapshot::WireEntity wire = engine::network::snapshot::Quantize(snapshotEntity);
			// Chantier 4.5 — PNJ d'ambiance mis à jour un snapshot sur deux.
			if (const MobEntity* mob = FindMobByEntityId(entityId); mob != nullptr && mob->ambient)
				wire.halfRate = true;
			current.push_back(std::move(wire));
		}

		engine::network::snapshot::ChunkHeader header{};
		header.clientId = client.clientId;
		header.serverTick = m_currentTick;
		header.connectedClients = static_cast<uint16_t>((std::min)(m_clients.size(), static_cast<size_t>(0xFFFFu)));
		header.receivedPackets = static_cast<uint32_t>((std::min)(m_transport.ReceivedPacketCount(), static_cast<uint64_t>(0xFFFFFFFFu)));
		header.sentPackets = static_cast<uint32_t>((std::min)(m_transport.SentPacketCount(), static_cast<uint64_t>(0xFFFFFFFFu)));
		const size_t entityCount = current.size();
		const std::vector<std::vector<std::byte>> packets = client.snapshotSender.BuildPackets(header, std::move(current));
		if (packets.empty())
		{
			LOG_WARN(Net, "[ServerApp] Snapshot non envoyé : trop gros (client_id={}, entities={})",
				client.clientId, entityCount);
			return false;
		}

		bool allSent = true;
		for (const std::vector<std::byte>& packet : packets)
		{
			if (!SendDatagram(client.endpoint, packet))
			{
				allSent = false;
			}
		}
		LOG_DEBUG(Net, "[ServerApp] Snapshot sent (client_id={}, tick={}, seq={}, base={}, entities={}, chunks={})",
			client.clientId, m_currentTick, client.snapshotSender.LastSentSeq(), client.snapshotSender.LastBaselineSeq(),
			entityCount, packets.size());
		return allSent;
	}

	ConnectedClient* ServerApp::FindClient(const Endpoint& endpoint)
	{
		// TG.2 — O(1) via index map (auparavant O(N) sur m_clients).
		const auto it = m_clientIndexByPackedEndpoint.find(PackEndpointKey(endpoint));
		if (it == m_clientIndexByPackedEndpoint.end() || it->second >= m_clients.size())
			return nullptr;
		return &m_clients[it->second];
	}

	const ConnectedClient* ServerApp::FindClient(const Endpoint& endpoint) const
	{
		const auto it = m_clientIndexByPackedEndpoint.find(PackEndpointKey(endpoint));
		if (it == m_clientIndexByPackedEndpoint.end() || it->second >= m_clients.size())
			return nullptr;
		return &m_clients[it->second];
	}

	ConnectedClient* ServerApp::FindClientByEntityId(EntityId entityId)
	{
		const auto it = m_clientIndexByEntityId.find(entityId);
		if (it == m_clientIndexByEntityId.end() || it->second >= m_clients.size())
			return nullptr;
		return &m_clients[it->second];
	}

	const ConnectedClient* ServerApp::FindClientByEntityId(EntityId entityId) const
	{
		const auto it = m_clientIndexByEntityId.find(entityId);
		if (it == m_clientIndexByEntityId.end() || it->second >= m_clients.size())
			return nullptr;
		return &m_clients[it->second];
	}

	void ServerApp::RebuildClientIndexes()
	{
		m_clientIndexByPackedEndpoint.clear();
		m_clientIndexByClientId.clear();
		m_clientIndexByEntityId.clear();
		m_clientIndexByPackedEndpoint.reserve(m_clients.size());
		m_clientIndexByClientId.reserve(m_clients.size());
		m_clientIndexByEntityId.reserve(m_clients.size());
		for (size_t i = 0; i < m_clients.size(); ++i)
		{
			const ConnectedClient& c = m_clients[i];
			m_clientIndexByPackedEndpoint[PackEndpointKey(c.endpoint)] = i;
			m_clientIndexByClientId[c.clientId] = i;
			m_clientIndexByEntityId[c.entityId] = i;
		}
	}

	MobEntity* ServerApp::FindMobByEntityId(EntityId entityId)
	{
		for (MobEntity& mob : m_mobs)
		{
			if (mob.entityId == entityId)
			{
				return &mob;
			}
		}
		return nullptr;
	}

	const MobEntity* ServerApp::FindMobByEntityId(EntityId entityId) const
	{
		for (const MobEntity& mob : m_mobs)
		{
			if (mob.entityId == entityId)
			{
				return &mob;
			}
		}
		return nullptr;
	}

	LootBagEntity* ServerApp::FindLootBagByEntityId(EntityId entityId)
	{
		for (LootBagEntity& lootBag : m_lootBags)
		{
			if (lootBag.entityId == entityId)
			{
				return &lootBag;
			}
		}
		return nullptr;
	}

	const LootBagEntity* ServerApp::FindLootBagByEntityId(EntityId entityId) const
	{
		for (const LootBagEntity& lootBag : m_lootBags)
		{
			if (lootBag.entityId == entityId)
			{
				return &lootBag;
			}
		}
		return nullptr;
	}

	CellGrid* ServerApp::GetOrCreateZoneGrid(uint32_t zoneId)
	{
		// Monde « Royaumes » : une grille par CARTE — toutes les cellules de 10 km
		// partagent la grille du continent (visibilité continue aux frontières).
		const uint32_t mapId = engine::world::cells::MapIdOfZone(zoneId);
		auto [it, inserted] = m_zoneGrids.try_emplace(mapId);
		if (inserted)
		{
			// Carte en cellules (Royaumes ou Enfers) : grille couvrant toute la carte.
			const bool cellWorldMap = engine::world::cells::CellMapFromMapId(mapId).has_value();
			const bool ok = cellWorldMap ? it->second.Init(kCellWorldHalfExtentMeters) : it->second.Init();
			if (!ok)
			{
				LOG_ERROR(Net, "[ServerApp] Zone grid init FAILED (zone_id={}, map_id={})", zoneId, mapId);
				m_zoneGrids.erase(it);
				return nullptr;
			}

			LOG_INFO(Net, "[ServerApp] Zone grid ready (zone_id={}, map_id={}, extent_m={:.0f})",
				zoneId, mapId, it->second.HalfExtentMeters() * 2.0f);
		}

		return &it->second;
	}
}
