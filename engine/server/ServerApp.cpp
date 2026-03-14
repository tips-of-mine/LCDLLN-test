#include "engine/server/ServerApp.h"

#include "engine/core/Log.h"
#include "engine/server/ServerProtocol.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace engine::server
{
	namespace
	{
		/// Fixed authoritative combat numbers for the MVP ticket.
		inline constexpr uint32_t kDefaultPlayerHealth = 100;
		inline constexpr uint32_t kDefaultMobHealth = 60;
		inline constexpr uint32_t kDefaultPlayerDamage = 10;
		inline constexpr uint32_t kDefaultMobArchetypeId = 100;
		inline constexpr EntityId kDefaultMobEntityId = 0x100000000ull;
		inline constexpr float kDefaultAttackRangeMeters = 4.0f;
		inline constexpr float kDefaultMobSpawnX = 128.0f;
		inline constexpr float kDefaultMobSpawnY = 0.0f;
		inline constexpr float kDefaultMobSpawnZ = 128.0f;

		/// Clamp a signed config integer into an unsigned 16-bit range.
		uint16_t ClampToU16(int64_t value, uint16_t minValue, uint16_t maxValue)
		{
			if (value < static_cast<int64_t>(minValue))
			{
				return minValue;
			}
			if (value > static_cast<int64_t>(maxValue))
			{
				return maxValue;
			}
			return static_cast<uint16_t>(value);
		}

		/// Return true when the entity id already exists in the list.
		bool ContainsEntityId(const std::vector<EntityId>& entityIds, EntityId entityId)
		{
			return std::find(entityIds.begin(), entityIds.end(), entityId) != entityIds.end();
		}

		/// Build the fixed MVP combat component using the current server tick rate.
		CombatComponent BuildDefaultCombatComponent(uint16_t tickHz, uint32_t damagePerHit)
		{
			CombatComponent component{};
			component.damagePerHit = damagePerHit;
			component.attackRangeMeters = kDefaultAttackRangeMeters;
			component.cooldownTicks = std::max<uint32_t>(1u, static_cast<uint32_t>(tickHz / 2u));
			component.nextAttackTick = 0;
			return component;
		}

		/// Return the squared XZ distance used by the range validation.
		float DistanceSquaredXZ(float ax, float az, float bx, float bz)
		{
			const float dx = ax - bx;
			const float dz = az - bz;
			return (dx * dx) + (dz * dz);
		}
	}

	ServerApp::ServerApp(engine::core::Config config)
		: m_config(std::move(config))
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
		m_tickHz = ResolveTickHz();
		m_snapshotHz = ResolveSnapshotHz(m_tickHz);
		m_nextClientId = 1;
		m_currentTick = 0;
		m_snapshotAccumulator = 0;
		m_stopRequested = false;
		m_clients.clear();
		m_mobs.clear();
		m_pendingDatagrams.clear();
		m_zoneGrids.clear();
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

		if (!InitCombatMobs())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: combat mob bootstrap failed");
			Shutdown();
			return false;
		}

		m_tickScheduler.Start();
		m_initialized = true;
		LOG_INFO(Core, "[ServerApp] Init OK (port={}, tick_hz={}, snapshot_hz={})",
			m_listenPort, m_tickHz, m_snapshotHz);
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
		while (!m_stopRequested)
		{
			if (!m_tickScheduler.WaitForNextTick())
			{
				LOG_ERROR(Core, "[ServerApp] Run FAILED: scheduler tick wait failed");
				return 1;
			}

			ProcessIncomingPackets();
			TickOnce();
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
		if (!m_initialized
			&& !m_transport.IsValid()
			&& m_clients.empty()
			&& m_mobs.empty()
			&& m_zoneGrids.empty()
			&& m_pendingDatagrams.empty())
		{
			return;
		}

		m_initialized = false;
		for (const ConnectedClient& client : m_clients)
		{
			if (client.hasCell)
			{
				const auto zoneIt = m_zoneGrids.find(client.zoneId);
				if (zoneIt != m_zoneGrids.end())
				{
					(void)zoneIt->second.RemoveEntity(client.entityId);
				}
			}
		}
		m_clients.clear();
		for (const MobEntity& mob : m_mobs)
		{
			const auto zoneIt = m_zoneGrids.find(mob.zoneId);
			if (zoneIt != m_zoneGrids.end())
			{
				(void)zoneIt->second.RemoveEntity(mob.entityId);
			}
		}
		m_mobs.clear();
		for (auto& [zoneId, zoneGrid] : m_zoneGrids)
		{
			(void)zoneId;
			zoneGrid.Shutdown();
		}
		m_zoneGrids.clear();
		m_pendingDatagrams.clear();
		m_tickScheduler.Shutdown();
		m_transport.Shutdown();
		m_zoneTransitionMap.Shutdown();
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

	void ServerApp::ProcessIncomingPackets()
	{
		m_transport.Receive(m_pendingDatagrams, 64);
		for (const Datagram& datagram : m_pendingDatagrams)
		{
			ProcessPacket(datagram);
		}
	}

	void ServerApp::ProcessPacket(const Datagram& datagram)
	{
		const std::span<const std::byte> packetBytes(datagram.bytes.data(), datagram.size);

		HelloMessage hello{};
		if (DecodeHello(packetBytes, hello))
		{
			HandleHello(datagram.endpoint, hello.clientNonce);
			return;
		}

		InputMessage input{};
		if (DecodeInput(packetBytes, input))
		{
			HandleInput(datagram.endpoint, input.clientId, input.inputSequence, input.positionMetersX, input.positionMetersZ);
			return;
		}

		AttackRequestMessage attackRequest{};
		if (DecodeAttackRequest(packetBytes, attackRequest))
		{
			HandleAttackRequest(datagram.endpoint, attackRequest.clientId, attackRequest.targetEntityId);
			return;
		}

		LOG_WARN(Net, "[ServerApp] Dropped invalid packet from {}", UdpTransport::EndpointToString(datagram.endpoint));
	}

	void ServerApp::HandleHello(const Endpoint& endpoint, uint32_t helloNonce)
	{
		ConnectedClient* existingClient = FindClient(endpoint);
		if (existingClient != nullptr)
		{
			existingClient->helloNonce = helloNonce;
			LOG_INFO(Net, "[ServerApp] Handshake refresh (client_id={}, endpoint={}, nonce={})",
				existingClient->clientId,
				UdpTransport::EndpointToString(endpoint),
				helloNonce);
			(void)SendWelcome(*existingClient);
			return;
		}

		ConnectedClient client{};
		client.endpoint = endpoint;
		client.clientId = m_nextClientId++;
		client.zoneId = 1;
		client.entityId = static_cast<EntityId>(client.clientId);
		client.helloNonce = helloNonce;
		client.stats.currentHealth = kDefaultPlayerHealth;
		client.stats.maxHealth = kDefaultPlayerHealth;
		client.combat = BuildDefaultCombatComponent(m_tickHz, kDefaultPlayerDamage);
		ConnectedClient& acceptedClient = m_clients.emplace_back(std::move(client));
		LOG_INFO(Net, "[ServerApp] Client accepted (client_id={}, entity_id={}, zone_id={}, hp={}, endpoint={}, total_clients={})",
			acceptedClient.clientId,
			acceptedClient.entityId,
			acceptedClient.zoneId,
			acceptedClient.stats.currentHealth,
			UdpTransport::EndpointToString(endpoint),
			m_clients.size());
		(void)SendWelcome(acceptedClient);
	}

	void ServerApp::HandleInput(const Endpoint& endpoint, uint32_t clientId, uint32_t inputSequence, float positionMetersX, float positionMetersZ)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] Input ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] Input ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		const float previousPositionX = client->positionMetersX;
		const float previousPositionZ = client->positionMetersZ;
		client->lastInputSequence = inputSequence;
		client->positionMetersX = positionMetersX;
		client->positionMetersZ = positionMetersZ;
		if (client->hasReplicatedState)
		{
			const float tickDt = 1.0f / static_cast<float>(m_tickHz);
			client->velocityMetersPerSecondX = (client->positionMetersX - previousPositionX) / tickDt;
			client->velocityMetersPerSecondZ = (client->positionMetersZ - previousPositionZ) / tickDt;
			if (std::abs(client->velocityMetersPerSecondX) > 0.001f || std::abs(client->velocityMetersPerSecondZ) > 0.001f)
			{
				client->yawRadians = std::atan2(client->velocityMetersPerSecondX, client->velocityMetersPerSecondZ);
			}
		}
		client->hasReplicatedState = true;
		MaybeApplyZoneTransition(*client);
		LOG_DEBUG(Net, "[ServerApp] Input accepted (client_id={}, seq={}, pos=({:.2f}, {:.2f}))",
			client->clientId,
			client->lastInputSequence,
			client->positionMetersX,
			client->positionMetersZ);
		UpdateClientInterest(*client);
	}

	void ServerApp::TickOnce()
	{
		++m_currentTick;
		Simulate();
		RefreshReplication();
		MaybeSendSnapshots();
		if ((m_currentTick % m_tickHz) == 0)
		{
			LOG_INFO(Core, "[ServerApp] Tick stats (tick={}, clients={}, rx_packets={}, tx_packets={})",
				m_currentTick,
				m_clients.size(),
				m_transport.ReceivedPacketCount(),
				m_transport.SentPacketCount());
		}
	}

	void ServerApp::Simulate()
	{
		LOG_TRACE(Core, "[ServerApp] Simulate tick {}", m_currentTick);
	}

	bool ServerApp::InitCombatMobs()
	{
		m_mobs.clear();

		CellGrid* zoneGrid = GetOrCreateZoneGrid(1);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Combat mob init FAILED: missing zone grid");
			return false;
		}

		MobEntity mob{};
		mob.entityId = kDefaultMobEntityId;
		mob.zoneId = 1;
		mob.archetypeId = kDefaultMobArchetypeId;
		mob.positionMetersX = kDefaultMobSpawnX;
		mob.positionMetersY = kDefaultMobSpawnY;
		mob.positionMetersZ = kDefaultMobSpawnZ;
		mob.stats.currentHealth = kDefaultMobHealth;
		mob.stats.maxHealth = kDefaultMobHealth;
		mob.combat = BuildDefaultCombatComponent(m_tickHz, kDefaultPlayerDamage);

		CellCoord cell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, cell))
		{
			LOG_ERROR(Net, "[ServerApp] Combat mob init FAILED: grid mapping failed (entity_id={})", mob.entityId);
			return false;
		}

		m_mobs.push_back(mob);
		LOG_INFO(Net,
			"[ServerApp] Combat mob ready (entity_id={}, zone_id={}, archetype_id={}, hp={}, pos=({:.2f}, {:.2f}, {:.2f}))",
			mob.entityId,
			mob.zoneId,
			mob.archetypeId,
			mob.stats.currentHealth,
			mob.positionMetersX,
			mob.positionMetersY,
			mob.positionMetersZ);
		return true;
	}

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
		for (const ConnectedClient& client : m_clients)
		{
			(void)SendSnapshot(client);
		}
	}

	void ServerApp::UpdateClientInterest(ConnectedClient& client)
	{
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
		(void)SendZoneChange(client, zoneChange);
	}

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

		if (target->zoneId != client->zoneId)
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

		const uint32_t appliedDamage = std::min(client->combat.damagePerHit, target->stats.currentHealth);
		if (appliedDamage == 0)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: zero damage after validation (client_id={}, target_entity_id={})",
				client->clientId,
				target->entityId);
			return;
		}

		client->combat.nextAttackTick = m_currentTick + client->combat.cooldownTicks;
		target->stats.currentHealth -= appliedDamage;
		if (target->stats.currentHealth == 0)
		{
			target->stateFlags |= kEntityStateDead;
			LOG_INFO(Net, "[ServerApp] Mob died (entity_id={}, attacker_entity_id={})", target->entityId, client->entityId);
		}

		CombatEventMessage combatEvent{};
		combatEvent.attackerEntityId = client->entityId;
		combatEvent.targetEntityId = target->entityId;
		combatEvent.damage = appliedDamage;
		combatEvent.targetCurrentHealth = target->stats.currentHealth;
		combatEvent.targetMaxHealth = target->stats.maxHealth;
		combatEvent.targetStateFlags = target->stateFlags;
		LOG_INFO(Net,
			"[ServerApp] Attack applied (attacker_entity_id={}, target_entity_id={}, damage={}, hp={}/{}, next_attack_tick={})",
			combatEvent.attackerEntityId,
			combatEvent.targetEntityId,
			combatEvent.damage,
			combatEvent.targetCurrentHealth,
			combatEvent.targetMaxHealth,
			client->combat.nextAttackTick);
		BroadcastCombatEvent(combatEvent);
	}

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

		BuildRelevantEntityIds(client, m_relevantEntityScratch);
		for (EntityId entityId : m_relevantEntityScratch)
		{
			if (!ContainsEntityId(client.replicatedEntityIds, entityId))
			{
				(void)SendSpawn(client, entityId);
			}
		}

		for (EntityId entityId : client.replicatedEntityIds)
		{
			if (!ContainsEntityId(m_relevantEntityScratch, entityId))
			{
				(void)SendDespawn(client, entityId);
			}
		}

		client.replicatedEntityIds = m_relevantEntityScratch;
	}

	void ServerApp::BuildRelevantEntityIds(const ConnectedClient& client, std::vector<EntityId>& outEntityIds) const
	{
		outEntityIds.clear();
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
				outEntityIds.push_back(entityId);
				continue;
			}

			if (FindMobByEntityId(entityId) != nullptr)
			{
				outEntityIds.push_back(entityId);
			}
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

	bool ServerApp::TryBuildSpawnEntity(EntityId entityId, SpawnEntity& outEntity) const
	{
		if (const ConnectedClient* client = FindClientByEntityId(entityId))
		{
			outEntity.entityId = client->entityId;
			outEntity.archetypeId = client->archetypeId;
			outEntity.state = BuildEntityState(*client);
			return true;
		}

		if (const MobEntity* mob = FindMobByEntityId(entityId))
		{
			outEntity.entityId = mob->entityId;
			outEntity.archetypeId = mob->archetypeId;
			outEntity.state = BuildEntityState(*mob);
			return true;
		}

		LOG_WARN(Net, "[ServerApp] Spawn build ignored: unknown entity_id={}", entityId);
		return false;
	}

	bool ServerApp::TryBuildSnapshotEntity(EntityId entityId, SnapshotEntity& outEntity) const
	{
		if (const ConnectedClient* client = FindClientByEntityId(entityId))
		{
			outEntity.entityId = client->entityId;
			outEntity.state = BuildEntityState(*client);
			return true;
		}

		if (const MobEntity* mob = FindMobByEntityId(entityId))
		{
			outEntity.entityId = mob->entityId;
			outEntity.state = BuildEntityState(*mob);
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

	bool ServerApp::SendSpawn(const ConnectedClient& receiver, EntityId subjectEntityId)
	{
		SpawnEntity entity{};
		if (!TryBuildSpawnEntity(subjectEntityId, entity))
		{
			LOG_WARN(Net, "[ServerApp] Spawn send skipped: unresolved entity_id={}", subjectEntityId);
			return false;
		}

		const std::vector<std::byte> packet = EncodeSpawn(entity);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Spawn send failed (receiver_client_id={}, entity_id={})",
				receiver.clientId,
				entity.entityId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Spawn sent (receiver_client_id={}, entity_id={}, archetype_id={})",
			receiver.clientId,
			entity.entityId,
			entity.archetypeId);
		return true;
	}

	bool ServerApp::SendDespawn(const ConnectedClient& receiver, EntityId entityId)
	{
		DespawnEntity entity{};
		entity.entityId = entityId;
		const std::vector<std::byte> packet = EncodeDespawn(entity);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Despawn send failed (receiver_client_id={}, entity_id={})",
				receiver.clientId,
				entityId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Despawn sent (receiver_client_id={}, entity_id={})",
			receiver.clientId,
			entityId);
		return true;
	}

	bool ServerApp::SendZoneChange(const ConnectedClient& receiver, const ZoneChangeMessage& message)
	{
		const std::vector<std::byte> packet = EncodeZoneChange(message);
		if (!m_transport.Send(receiver.endpoint, packet))
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
		if (!m_transport.Send(receiver.endpoint, packet))
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

	bool ServerApp::SendWelcome(const ConnectedClient& client)
	{
		WelcomeMessage welcome{};
		welcome.clientId = client.clientId;
		welcome.tickHz = m_tickHz;
		welcome.snapshotHz = m_snapshotHz;
		const std::vector<std::byte> packet = EncodeWelcome(welcome);
		if (!m_transport.Send(client.endpoint, packet))
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

	bool ServerApp::SendSnapshot(const ConnectedClient& client)
	{
		m_snapshotEntitiesScratch.clear();
		for (EntityId entityId : client.replicatedEntityIds)
		{
			SnapshotEntity snapshotEntity{};
			if (!TryBuildSnapshotEntity(entityId, snapshotEntity))
			{
				continue;
			}

			m_snapshotEntitiesScratch.push_back(snapshotEntity);
		}

		SnapshotMessage snapshot{};
		snapshot.clientId = client.clientId;
		snapshot.serverTick = m_currentTick;
		snapshot.connectedClients = static_cast<uint16_t>(std::min<size_t>(m_clients.size(), 0xFFFFu));
		snapshot.entityCount = static_cast<uint16_t>(std::min<size_t>(m_snapshotEntitiesScratch.size(), 0xFFFFu));
		snapshot.receivedPackets = static_cast<uint32_t>(std::min<uint64_t>(m_transport.ReceivedPacketCount(), 0xFFFFFFFFu));
		snapshot.sentPackets = static_cast<uint32_t>(std::min<uint64_t>(m_transport.SentPacketCount(), 0xFFFFFFFFu));
		const std::vector<std::byte> packet = EncodeSnapshot(snapshot, m_snapshotEntitiesScratch);
		if (!m_transport.Send(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Snapshot send failed (client_id={}, tick={})", client.clientId, m_currentTick);
			return false;
		}

		LOG_DEBUG(Net, "[ServerApp] Snapshot sent (client_id={}, tick={}, entity_count={})",
			client.clientId,
			m_currentTick,
			m_snapshotEntitiesScratch.size());
		return true;
	}

	ConnectedClient* ServerApp::FindClient(const Endpoint& endpoint)
	{
		for (ConnectedClient& client : m_clients)
		{
			if (client.endpoint == endpoint)
			{
				return &client;
			}
		}
		return nullptr;
	}

	const ConnectedClient* ServerApp::FindClient(const Endpoint& endpoint) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (client.endpoint == endpoint)
			{
				return &client;
			}
		}
		return nullptr;
	}

	const ConnectedClient* ServerApp::FindClientByEntityId(EntityId entityId) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (client.entityId == entityId)
			{
				return &client;
			}
		}
		return nullptr;
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

	CellGrid* ServerApp::GetOrCreateZoneGrid(uint32_t zoneId)
	{
		auto [it, inserted] = m_zoneGrids.try_emplace(zoneId);
		if (inserted)
		{
			if (!it->second.Init())
			{
				LOG_ERROR(Net, "[ServerApp] Zone grid init FAILED (zone_id={})", zoneId);
				m_zoneGrids.erase(it);
				return nullptr;
			}

			LOG_INFO(Net, "[ServerApp] Zone grid ready (zone_id={})", zoneId);
		}

		return &it->second;
	}
}
