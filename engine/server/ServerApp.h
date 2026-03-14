#pragma once

#include "engine/core/Config.h"
#include "engine/server/ReplicationTypes.h"
#include "engine/server/SpatialPartition.h"
#include "engine/server/TickScheduler.h"
#include "engine/server/UdpTransport.h"
#include "engine/server/ZoneTransitions.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// One connected client tracked by endpoint and assigned server id.
	struct ConnectedClient
	{
		Endpoint endpoint{};
		uint32_t clientId = 0;
		uint32_t zoneId = 1;
		EntityId entityId = 0;
		uint32_t archetypeId = 1;
		uint32_t lastInputSequence = 0;
		uint32_t helloNonce = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		float yawRadians = 0.0f;
		float velocityMetersPerSecondX = 0.0f;
		float velocityMetersPerSecondY = 0.0f;
		float velocityMetersPerSecondZ = 0.0f;
		uint32_t stateFlags = 0;
		StatsComponent stats{};
		CombatComponent combat{};
		CellCoord currentCell{};
		bool hasCell = false;
		bool hasReplicatedState = false;
		std::vector<CellCoord> interestCells;
		std::vector<EntityId> interestEntityIds;
		std::vector<EntityId> replicatedEntityIds;
	};

	/// Minimal authoritative mob replicated through the same interest system as players.
	struct MobEntity
	{
		EntityId entityId = 0;
		uint32_t zoneId = 1;
		uint32_t archetypeId = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		float yawRadians = 0.0f;
		float velocityMetersPerSecondX = 0.0f;
		float velocityMetersPerSecondY = 0.0f;
		float velocityMetersPerSecondZ = 0.0f;
		uint32_t stateFlags = 0;
		StatsComponent stats{};
		CombatComponent combat{};
	};

	/// Headless authoritative server skeleton with fixed ticks, replication and minimal combat.
	class ServerApp final
	{
	public:
		/// Store the merged config used by the server runtime.
		explicit ServerApp(engine::core::Config config);

		/// Ensure transport resources are released on teardown.
		~ServerApp();

		/// Initialize transport, scheduler and runtime settings.
		bool Init();

		/// Run the fixed tick loop until a stop is requested.
		int Run();

		/// Request a graceful shutdown from the main loop.
		void RequestStop();

		/// Release transport resources and emit shutdown logs.
		void Shutdown();

	private:
		/// Clamp and validate the configured fixed tick rate.
		uint16_t ResolveTickHz() const;

		/// Clamp and validate the configured snapshot send rate.
		uint16_t ResolveSnapshotHz(uint16_t tickHz) const;

		/// Receive pending datagrams and dispatch them to protocol handlers.
		void ProcessIncomingPackets();

		/// Handle one protocol packet coming from a client endpoint.
		void ProcessPacket(const Datagram& datagram);

		/// Accept a new client or refresh an existing handshake.
		void HandleHello(const Endpoint& endpoint, uint32_t helloNonce);

		/// Record the last input sequence for a connected client.
		void HandleInput(const Endpoint& endpoint, uint32_t clientId, uint32_t inputSequence, float positionMetersX, float positionMetersZ);

		/// Advance one authoritative server tick.
		void TickOnce();

		/// Run the placeholder simulation step for the current tick.
		void Simulate();

		/// Seed the minimal authoritative combat entities required by the ticket.
		bool InitCombatMobs();

		/// Build and send empty snapshots according to the snapshot cadence.
		void MaybeSendSnapshots();

		/// Update the client's grid cell and recompute its interest set if needed.
		void UpdateClientInterest(ConnectedClient& client);

		/// Validate and apply a server-authoritative zone transition when a player enters a transition volume.
		void MaybeApplyZoneTransition(ConnectedClient& client);

		/// Validate one attack request, apply damage and broadcast the authoritative event.
		void HandleAttackRequest(const Endpoint& endpoint, uint32_t clientId, EntityId targetEntityId);

		/// Refresh spawn/despawn replication for every connected client.
		void RefreshReplication();

		/// Refresh spawn/despawn replication for one connected client.
		void RefreshReplicationForClient(ConnectedClient& client);

		/// Build the list of relevant replicated entities for one client.
		void BuildRelevantEntityIds(const ConnectedClient& client, std::vector<EntityId>& outEntityIds) const;

		/// Convert one connected client to the minimal replicated entity state.
		EntityState BuildEntityState(const ConnectedClient& client) const;

		/// Convert one replicated mob to the minimal replicated entity state.
		EntityState BuildEntityState(const MobEntity& mob) const;

		/// Fill a spawn payload for a replicated player or mob entity.
		bool TryBuildSpawnEntity(EntityId entityId, SpawnEntity& outEntity) const;

		/// Fill a snapshot payload for a replicated player or mob entity.
		bool TryBuildSnapshotEntity(EntityId entityId, SnapshotEntity& outEntity) const;

		/// Return true when the client should receive the combat event broadcast.
		bool ShouldBroadcastCombatEventToClient(const ConnectedClient& receiver, EntityId attackerEntityId, EntityId targetEntityId) const;

		/// Broadcast one authoritative combat event to interested clients.
		void BroadcastCombatEvent(const CombatEventMessage& message);

		/// Send a spawn packet for one relevant entity.
		bool SendSpawn(const ConnectedClient& receiver, EntityId subjectEntityId);

		/// Send a despawn packet for one relevant entity.
		bool SendDespawn(const ConnectedClient& receiver, EntityId entityId);

		/// Send a combat event packet to one interested client.
		bool SendCombatEvent(const ConnectedClient& receiver, const CombatEventMessage& message);

		/// Send a zone change packet after a validated authoritative transition.
		bool SendZoneChange(const ConnectedClient& receiver, const ZoneChangeMessage& message);

		/// Send a welcome packet to the given connected client.
		bool SendWelcome(const ConnectedClient& client);

		/// Send one empty snapshot packet to the given connected client.
		bool SendSnapshot(const ConnectedClient& client);

		/// Find a connected client by endpoint.
		ConnectedClient* FindClient(const Endpoint& endpoint);

		/// Find a connected client by endpoint.
		const ConnectedClient* FindClient(const Endpoint& endpoint) const;

		/// Find a connected client by replicated entity id.
		const ConnectedClient* FindClientByEntityId(EntityId entityId) const;

		/// Find a mob by replicated entity id.
		MobEntity* FindMobByEntityId(EntityId entityId);

		/// Find a mob by replicated entity id.
		const MobEntity* FindMobByEntityId(EntityId entityId) const;

		/// Return the cell grid attached to the requested zone, creating it on first use.
		CellGrid* GetOrCreateZoneGrid(uint32_t zoneId);

		engine::core::Config m_config;
		ZoneTransitionMap m_zoneTransitionMap;
		TickScheduler m_tickScheduler;
		UdpTransport m_transport;
		std::vector<Datagram> m_pendingDatagrams;
		std::vector<ConnectedClient> m_clients;
		std::vector<MobEntity> m_mobs;
		std::unordered_map<uint32_t, CellGrid> m_zoneGrids;
		std::vector<EntityId> m_relevantEntityScratch;
		std::vector<SnapshotEntity> m_snapshotEntitiesScratch;
		uint16_t m_listenPort = 0;
		uint16_t m_tickHz = 0;
		uint16_t m_snapshotHz = 0;
		uint32_t m_nextClientId = 1;
		uint32_t m_currentTick = 0;
		uint32_t m_snapshotAccumulator = 0;
		bool m_initialized = false;
		bool m_stopRequested = false;
	};
}
