#pragma once

#include "src/shared/core/Config.h"
#include "src/shared/network/ReplicationTypes.h"
#include "src/shared/network/ServerProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	struct ConnectedClient;

	/// One loot entry in a resource node type definition (M36.1).
	struct ResourceNodeLootEntry
	{
		uint32_t itemId  = 0;
		uint32_t minQty  = 1;
		uint32_t maxQty  = 1;
	};

	/// Data-driven definition for one resource node type loaded from JSON (M36.1).
	struct ResourceNodeTypeDefinition
	{
		uint32_t typeId = 0;
		/// Human-readable type key: "ore_vein", "herb", "tree", "animal_corpse".
		std::string nodeType;
		/// Total harvest cast time in seconds (2-5 s per spec).
		float harvestTimeSeconds = 3.0f;
		/// Time before the node becomes available again (seconds).
		uint32_t respawnSeconds = 600;
		/// Maximum distance from player to node centre to allow harvesting (metres).
		float harvestRangeMeters = 5.0f;
		/// Possible item rewards rolled on harvest completion.
		std::vector<ResourceNodeLootEntry> lootTable;
	};

	/// Spawn position for one resource node instance in a zone (M36.1).
	struct ResourceNodeDefinition
	{
		std::string nodeId;
		uint32_t typeId = 0;
		uint32_t zoneId = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
	};

	/// Live runtime state for one spawned resource node (M36.1).
	struct ResourceNodeRuntimeState
	{
		ResourceNodeDefinition definition{};
		EntityId entityId = 0;
		/// True when the node can be harvested; false while depleted.
		bool available = true;
		/// Server tick when the node becomes available again (0 = already available).
		uint32_t respawnTick = 0;
	};

	/// One active harvest cast in progress (M36.1).
	struct HarvestSessionState
	{
		uint32_t clientId       = 0;
		EntityId nodeEntityId   = 0;
		uint32_t completionTick = 0;
		/// Player position at harvest start — used to detect movement cancellation.
		float startPositionX = 0.0f;
		float startPositionZ = 0.0f;
	};

	/// Result of a TryStartHarvest call (M36.1).
	enum class HarvestOpResult : uint8_t
	{
		Ok               = 0,
		NodeNotFound     = 1,
		NodeNotAvailable = 2,
		OutOfRange       = 3,
		AlreadyHarvesting = 4,
	};

	/// Maximum distance a player may move during a harvest before cancellation (M36.1).
	inline constexpr float kHarvestMoveCancelThresholdMeters = 1.5f;

	/// Server-side resource node spawner and harvest session manager (M36.1).
	///
	/// Lifecycle:
	///   1. Load node type definitions from `gathering/resource_nodes.json`.
	///   2. Load node spawn positions from `zones/*/gathering_nodes.json`.
	///   3. Assign stable EntityId to each node (server-side only; no replication).
	///   4. On `TryStartHarvest`, validate proximity and start a session.
	///   5. `Tick()` advances sessions; calls back on completion or cancellation.
	class GatheringSystem final
	{
	public:
		GatheringSystem() = default;

		/// Initialize and load all definitions.
		/// @param config       Engine config used to resolve content paths.
		/// @param tickHz       Server tick rate — used to convert seconds to ticks.
		/// @param firstEntityId Starting EntityId for node entity assignment.
		bool Init(const engine::core::Config& config, uint16_t tickHz, EntityId& inOutNextEntityId);

		/// Persist nothing; emit shutdown log.
		void Shutdown();

		// ------------------------------------------------------------------
		// Harvest session API (called from ServerApp handlers)
		// ------------------------------------------------------------------

		/// Try to start a harvest session for \p client on the node at \p nodeEntityId.
		/// @param outDurationTicks  Filled with the cast duration on success.
		HarvestOpResult TryStartHarvest(
			const ConnectedClient& client,
			EntityId               nodeEntityId,
			uint32_t               currentTick,
			uint32_t&              outDurationTicks);

		/// Cancel any active harvest session owned by \p clientId.
		/// Returns the cancelled nodeEntityId (0 if none was active).
		EntityId CancelHarvest(uint32_t clientId);

		// ------------------------------------------------------------------
		// Tick — advances sessions and fires completion callbacks
		// ------------------------------------------------------------------

		/// Advance harvest sessions.  For each session that completed this tick:
		/// - Rolls the loot table for the associated node type.
		/// - Stores the rolled items in \p outCompletedItems (same index as
		///   \p outCompletedNodeEntityIds).
		/// - Marks the node as depleted and arms the respawn timer.
		///
		/// Callers are responsible for sending HarvestComplete + InventoryDelta.
		void Tick(
			uint32_t                              currentTick,
			const std::vector<ConnectedClient>&   clients,
			std::vector<EntityId>&                outCompletedNodeEntityIds,
			std::vector<uint32_t>&                outCompletedClientIds,
			std::vector<std::vector<ItemStack>>&  outCompletedItems,
			std::vector<EntityId>&                outMoveCancelledNodeEntityIds,
			std::vector<uint32_t>&                outMoveCancelledClientIds);

		/// Return the node with \p entityId, or nullptr.
		ResourceNodeRuntimeState*       FindNode(EntityId entityId);
		const ResourceNodeRuntimeState* FindNode(EntityId entityId) const;

		/// Return the active harvest session for \p clientId, or nullptr.
		HarvestSessionState* FindSession(uint32_t clientId);

		bool IsInitialized() const { return m_initialized; }

		size_t NodeCount()    const { return m_nodes.size(); }
		size_t SessionCount() const { return m_sessions.size(); }

	private:
		/// Load node type definitions from `gathering/resource_nodes.json`.
		bool LoadNodeTypes(const engine::core::Config& config);

		/// Load zone-specific node spawn positions from `zones/*/gathering_nodes.json`.
		bool LoadZoneNodes(const engine::core::Config& config, EntityId& inOutNextEntityId);

		/// Look up a node type by typeId, or nullptr.
		const ResourceNodeTypeDefinition* FindNodeType(uint32_t typeId) const;

		/// Roll one loot result for a node type.
		std::vector<ItemStack> RollLoot(const ResourceNodeTypeDefinition& typeDef) const;

		/// Euclidean 2D distance between two XZ positions.
		static float Distance2D(float ax, float az, float bx, float bz);

		std::vector<ResourceNodeTypeDefinition> m_nodeTypes;
		std::vector<ResourceNodeRuntimeState>   m_nodes;
		std::vector<HarvestSessionState>        m_sessions;
		uint16_t m_tickHz          = 20;
		bool     m_initialized     = false;
	};
}
