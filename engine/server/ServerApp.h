#pragma once

#include "engine/server/CharacterPersistence.h"
#include "engine/server/EventRuntime.h"
#include "engine/server/FriendSystem.h"
#include "engine/server/QuestRuntime.h"
#include "engine/server/SpawnerRuntime.h"
#include "engine/core/Config.h"
#include "engine/net/ChatSystem.h"
#include "engine/server/ChatCommandParser.h"
#include "engine/server/ReplicationTypes.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/server/ServerProtocol.h"
#include "engine/server/SpatialPartition.h"
#include "engine/server/TickScheduler.h"
#include "engine/server/UdpTransport.h"
#include "engine/server/ZoneTransitions.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::server
{
	/// Minimal mob AI states required by the authoritative aggro ticket.
	enum class MobAiState : uint8_t
	{
		Idle = 0,
		Patrol = 1,
		Aggro = 2,
		Return = 3
	};

	/// One threat contribution stored on the mob authoritative table.
	struct ThreatEntry
	{
		EntityId entityId = 0;
		uint32_t threat = 0;
	};

	/// Visibility mode applied to one authoritative loot bag.
	enum class LootVisibility : uint8_t
	{
		Owner = 0,
		Public = 1
	};

	/// Runtime status stored for one dynamic zone event.
	enum class DynamicEventStatus : uint8_t
	{
		Idle = 0,
		Active = 1,
		Cooldown = 2
	};

	/// One row loaded from the loot table data file.
	struct LootTableEntry
	{
		uint32_t sourceArchetypeId = 0;
		ItemStack item{};
		LootVisibility visibility = LootVisibility::Owner;
	};

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
		uint32_t persistenceCharacterKey = 0;
		uint32_t experiencePoints = 0;
		uint32_t gold = 0;
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
		std::vector<ItemStack> inventory;
		std::vector<QuestState> questStates;
		/// M29.2: ignored chat senders (display names, persisted).
		std::vector<std::string> chatIgnoredDisplayNames;
		/// M29.2: may use `/kick`, `/ban`, `/mute`, `/announce`.
		bool chatModeratorRole = false;
		/// M29.2: block chat sends until this tick (0 = not muted).
		uint32_t chatMutedUntilServerTick = 0;
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
		float homePositionMetersX = 0.0f;
		float homePositionMetersY = 0.0f;
		float homePositionMetersZ = 0.0f;
		float patrolTargetMetersX = 0.0f;
		float patrolTargetMetersZ = 0.0f;
		float leashDistanceMeters = 0.0f;
		float moveSpeedMetersPerSecond = 0.0f;
		MobAiState aiState = MobAiState::Idle;
		EntityId aggroTargetEntityId = 0;
		uint32_t nextAiTick = 0;
		uint32_t nextPatrolTick = 0;
		uint32_t owningSpawnerIndex = 0;
		uint32_t owningSpawnerSlot = 0;
		uint32_t owningEventIndex = 0;
		uint32_t owningEventPhaseIndex = 0;
		bool patrolForward = true;
		bool pendingDespawn = false;
		bool isDynamicEventMob = false;
		std::vector<ThreatEntry> threatTable;
		bool hasSpawnedLoot = false;
	};

	/// One spawn slot tracked by the authoritative spawner runtime.
	struct SpawnerSlotState
	{
		EntityId mobEntityId = 0;
		uint32_t nextRespawnTick = 0;
	};

	/// One loaded spawner with runtime activation and respawn state.
	struct SpawnerRuntimeState
	{
		SpawnerDefinition definition{};
		CellCoord centerCell{};
		bool isActive = false;
		std::vector<SpawnerSlotState> slots;
	};

	/// One dynamic event runtime state advanced by the authoritative server tick.
	struct DynamicEventState
	{
		DynamicEventDefinition definition{};
		DynamicEventStatus status = DynamicEventStatus::Idle;
		uint32_t currentPhaseIndex = 0;
		uint32_t currentPhaseProgress = 0;
		uint32_t nextTriggerTick = 0;
		uint32_t cooldownUntilTick = 0;
		std::vector<EntityId> phaseMobEntityIds;
		std::vector<uint32_t> participantClientIds;
	};

	/// Minimal authoritative loot bag spawned from a mob death.
	struct LootBagEntity
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
		LootVisibility visibility = LootVisibility::Owner;
		EntityId ownerEntityId = 0;
		std::vector<ItemStack> items;
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

		/// Load the data-driven spawners and prepare their runtime state.
		bool InitSpawners();

		/// Resolve the autosave cadence required by the persistence ticket.
		uint32_t ResolveCharacterAutosaveIntervalTicks() const;

		/// Persist every connected character when the autosave timer elapses.
		void MaybeAutosaveCharacters();

		/// Persist one connected character to the current runtime store.
		void SaveConnectedClient(const ConnectedClient& client, std::string_view reason);

		/// Load the authoritative loot table data required by the ticket.
		bool InitLootTables();

		/// Load the data-driven quest definitions required by M15.1.
		bool InitQuests();

		/// Load the data-driven dynamic event definitions required by M15.3.
		bool InitDynamicEvents();

		/// Refresh spawner activation, despawn inactive mobs and process respawns.
		void UpdateSpawners();

		/// Advance event triggers, phases, rewards and notifications.
		void UpdateDynamicEvents();

		/// Update the mob AI state machine at a reduced fixed cadence.
		void UpdateMobAi();

		/// Advance one mob through the authoritative AI state machine.
		void UpdateMobAi(MobEntity& mob);

		/// Return true when at least one player is close enough to activate the spawner.
		bool IsSpawnerActivatedByPlayers(const SpawnerRuntimeState& spawner) const;

		/// Return true when one living spawned mob is still in combat.
		bool SpawnerHasCombat(const SpawnerRuntimeState& spawner) const;

		/// Spawn one authoritative mob for the requested spawner slot.
		bool SpawnMobFromSpawner(SpawnerRuntimeState& spawner, uint32_t slotIndex);

		/// Despawn one authoritative mob and optionally arm its respawn timer.
		bool DespawnSpawnerMob(SpawnerRuntimeState& spawner, uint32_t slotIndex, std::string_view reason, bool scheduleRespawn);

		/// Schedule the next activation tick for one dynamic event.
		void ScheduleDynamicEventTrigger(DynamicEventState& eventState, bool fromCooldown);

		/// Start one dynamic event and spawn its first phase.
		bool StartDynamicEvent(DynamicEventState& eventState);

		/// Spawn the currently active phase mobs for one dynamic event.
		bool SpawnDynamicEventPhase(DynamicEventState& eventState);

		/// Spawn one authoritative mob for one dynamic event phase.
		bool SpawnMobForDynamicEvent(
			DynamicEventState& eventState,
			uint32_t phaseIndex,
			const DynamicEventSpawnDefinition& spawnDefinition,
			uint32_t spawnOrdinal);

		/// Despawn one authoritative mob currently owned by a dynamic event.
		bool DespawnDynamicEventMob(DynamicEventState& eventState, EntityId entityId, std::string_view reason);

		/// Advance one event to the next phase or finish it when every phase completed.
		bool AdvanceDynamicEventPhase(DynamicEventState& eventState);

		/// Complete one dynamic event and grant rewards to recorded participants.
		void CompleteDynamicEvent(DynamicEventState& eventState);

		/// Record one player as participant of the given dynamic event.
		void AddDynamicEventParticipant(DynamicEventState& eventState, const ConnectedClient& client);

		/// Return true when at least one active player is present in the requested zone.
		bool HasPlayersInZone(uint32_t zoneId) const;

		/// Return the number of server ticks between two AI updates.
		uint32_t ResolveMobAiIntervalTicks() const;

		/// Build and send empty snapshots according to the snapshot cadence.
		void MaybeSendSnapshots();

		/// Update the client's grid cell and recompute its interest set if needed.
		void UpdateClientInterest(ConnectedClient& client);

		/// Validate and apply a server-authoritative zone transition when a player enters a transition volume.
		void MaybeApplyZoneTransition(ConnectedClient& client);

		/// Validate one attack request, apply damage and broadcast the authoritative event.
		void HandleAttackRequest(const Endpoint& endpoint, uint32_t clientId, EntityId targetEntityId);

		/// Validate one pickup request, update the inventory and despawn the bag.
		void HandlePickupRequest(const Endpoint& endpoint, uint32_t clientId, EntityId lootBagEntityId);

		/// Validate one talk request and forward it to the quest runtime.
		void HandleTalkRequest(const Endpoint& endpoint, uint32_t clientId, std::string_view targetId);

		/// Validate one chat send request, apply rate limiting, route by channel, emit relays.
		void HandleChatSend(const Endpoint& endpoint, const ChatSendRequestMessage& request);

		/// Update one mob threat table from an authoritative combat event.
		void UpdateThreatFromCombatEvent(const CombatEventMessage& message);

		/// Spawn one authoritative loot bag from a dead mob using the loaded loot tables.
		void SpawnLootBagForMob(const MobEntity& mob, EntityId ownerEntityId);

		/// Refresh the mob aggro target from the current threat table.
		void RefreshMobAggroTarget(MobEntity& mob);

		/// Move one mob toward a target point and update its replicated transform.
		bool MoveMobTowards(MobEntity& mob, float targetPositionX, float targetPositionZ);

		/// Synchronize one mob position with the zone spatial partition.
		bool UpdateMobSpatialState(MobEntity& mob);

		/// Transition one mob to a new authoritative AI state.
		void SetMobAiState(MobEntity& mob, MobAiState newState);

		/// Clear one mob threat table and active aggro target.
		void ResetMobThreat(MobEntity& mob);

		/// Apply one mob attack against its current target when in range.
		bool TryMobAttackPlayer(MobEntity& mob, ConnectedClient& target);

		/// Add one item stack to the player's authoritative inventory.
		void AddItemToInventory(ConnectedClient& client, const ItemStack& item);

		/// Apply one authoritative quest event and emit any resulting deltas.
		void ApplyQuestEvent(ConnectedClient& client, QuestStepType eventType, std::string_view targetId, uint32_t amount, std::string_view reason);

		/// Send the current quest journal state after the client handshake succeeds.
		void SendQuestStateBootstrap(const ConnectedClient& receiver);

		/// Send the current event state snapshot for the receiver's zone.
		void SendDynamicEventBootstrap(const ConnectedClient& receiver);

		/// Send one inventory delta after a successful pickup.
		bool SendInventoryDelta(const ConnectedClient& receiver, std::span<const ItemStack> items);

		/// Send one quest delta after a successful quest state update.
		bool SendQuestDelta(const ConnectedClient& receiver, const QuestProgressDelta& delta);

		/// Send one dynamic event state update to one client.
		bool SendDynamicEventState(
			const ConnectedClient& receiver,
			const DynamicEventState& eventState,
			std::string_view notificationText,
			uint32_t rewardExperience,
			uint32_t rewardGold,
			std::span<const ItemStack> rewardItems);

		/// Broadcast one dynamic event state update to clients in the same zone.
		void BroadcastDynamicEventState(
			const DynamicEventState& eventState,
			std::string_view notificationText,
			uint32_t rewardedClientId,
			uint32_t rewardExperience,
			uint32_t rewardGold,
			std::span<const ItemStack> rewardItems);

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

		/// Convert one replicated loot bag to the minimal replicated entity state.
		EntityState BuildEntityState(const LootBagEntity& lootBag) const;

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

		/// Send one chat relay line to one client (M29.1).
		bool SendChatRelay(const ConnectedClient& receiver, const ChatRelayMessage& message);

		/// Send one emote relay event to one client (M29.3).
		bool SendEmoteRelay(const ConnectedClient& receiver, const EmoteRelayMessage& message);

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
		ConnectedClient* FindClientByEntityId(EntityId entityId);

		/// Find a connected client by replicated entity id.
		const ConnectedClient* FindClientByEntityId(EntityId entityId) const;

		/// Find a mob by replicated entity id.
		MobEntity* FindMobByEntityId(EntityId entityId);

		/// Find a mob by replicated entity id.
		const MobEntity* FindMobByEntityId(EntityId entityId) const;

		/// Find a loot bag by replicated entity id.
		LootBagEntity* FindLootBagByEntityId(EntityId entityId);

		/// Find a loot bag by replicated entity id.
		const LootBagEntity* FindLootBagByEntityId(EntityId entityId) const;

		/// Return the cell grid attached to the requested zone, creating it on first use.
		CellGrid* GetOrCreateZoneGrid(uint32_t zoneId);

		/// Initialize moderation audit log (M29.2).
		void InitModerationAuditSubsystem();

		/// Load banned character keys from content-relative ban list file.
		void LoadChatBanFile();

		/// Persist banned character keys to content-relative ban list file.
		void SaveChatBanFile();

		/// Send one system line to a single client (M29.2).
		void SendChatSystemNotice(ConnectedClient& receiver, std::string_view text);

		/// Broadcast a global moderation announcement (M29.2).
		void BroadcastModerationAnnouncement(std::string_view text);

		/// Handle one parsed slash command; returns true when handled (no normal chat relay).
		bool HandleChatSlashCommand(ConnectedClient& sender, const ParsedChatSlashCommand& command);

		/// Resolve `P<id>` style display token to a connected client, or nullptr.
		ConnectedClient* FindConnectedClientByChatDisplayName(std::string_view displayToken);

		/// Disconnect and persist one client (e.g. `/kick`).
		void DisconnectConnectedClient(uint32_t clientId, std::string_view persistenceReason);

		/// True when \p receiver has ignored \p senderDisplay (case-insensitive ASCII).
		bool IsChatSenderIgnoredBy(const ConnectedClient& receiver, std::string_view senderDisplay) const;

		/// Report line to audit file or main log fallback.
		void AuditLogChatReport(std::string_view reporterDisplay, std::string_view targetDisplay, std::string_view reason);

		/// Moderation action to audit file or main log fallback.
		void AuditLogModeration(std::string_view action, std::string_view actorDisplay, std::string_view targetDisplay, std::string_view detail);

		// ------------------------------------------------------------------
		// M32.1 — Friend system helpers
		// ------------------------------------------------------------------

		/// Set a player online in the friend presence system and send FriendListSync.
		/// Call on successful client handshake (HandleHello, new client only).
		void OnClientLogin(ConnectedClient& client);

		/// Set a player offline in the friend presence system and notify online friends.
		/// Call before removing the client from m_clients.
		void OnClientLogout(const ConnectedClient& client);

		/// Send FriendListSync packet to \p receiver containing all friends with presence status.
		void SendFriendListSync(const ConnectedClient& receiver);

		/// Broadcast a FriendStatusUpdate to all online friends of \p subject.
		void BroadcastFriendStatusUpdate(const ConnectedClient& subject, PresenceStatus status);

		/// Handle /friend sub-command dispatched from HandleChatSlashCommand.
		bool HandleFriendCommand(ConnectedClient& sender, std::string_view argsRemainder);

		engine::core::Config m_config;
		CharacterPersistenceStore m_characterPersistence;
		EventRuntime m_eventRuntime;
		QuestRuntime m_questRuntime;
		SpawnerRuntime m_spawnerRuntime;
		ZoneTransitionMap m_zoneTransitionMap;
		TickScheduler m_tickScheduler;
		UdpTransport m_transport;
		std::vector<Datagram> m_pendingDatagrams;
		std::vector<ConnectedClient> m_clients;
		std::vector<MobEntity> m_mobs;
		std::vector<LootBagEntity> m_lootBags;
		std::vector<LootTableEntry> m_lootTableEntries;
		std::vector<DynamicEventState> m_dynamicEvents;
		std::vector<SpawnerRuntimeState> m_spawners;
		std::unordered_map<uint32_t, CellGrid> m_zoneGrids;
		std::vector<EntityId> m_relevantEntityScratch;
		std::vector<SnapshotEntity> m_snapshotEntitiesScratch;
		EntityId m_nextServerEntityId = 0x200000000ull;
		uint16_t m_listenPort = 0;
		uint16_t m_tickHz = 0;
		uint16_t m_snapshotHz = 0;
		uint32_t m_nextClientId = 1;
		uint32_t m_currentTick = 0;
		uint32_t m_characterAutosaveIntervalTicks = 0;
		uint32_t m_nextCharacterAutosaveTick = 0;
		uint32_t m_snapshotAccumulator = 0;
		bool m_initialized = false;
		bool m_stopRequested = false;

		/// Per-player chat send rate limiting (M29.1).
		engine::net::ChatRateLimiter m_chatRateLimiter;

		/// M29.2: moderation audit file (chat reports + admin actions).
		SecurityAuditLog m_moderationAuditLog;
		bool m_moderationAuditLogReady = false;
		/// M29.2: character keys rejected at handshake after `/ban`.
		std::unordered_set<uint32_t> m_bannedCharacterKeys;

		/// M32.1: friend system (presence tracking + DB-backed requests when ENGINE_HAS_MYSQL).
		FriendSystem m_friendSystem;
	};
}
