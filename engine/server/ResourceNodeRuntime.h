#pragma once

#include "engine/core/Config.h"
#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace engine::server
{
	/// Maximum proximity in metres for harvest interaction validation (M36.1).
	inline constexpr float kHarvestMaxRangeMeters = 5.0f;

	/// Maximum movement in metres (from harvest start) before the harvest is cancelled (M36.1).
	inline constexpr float kHarvestMoveToleranceMeters = 1.0f;

	/// One weighted loot entry in a resource node type definition (M36.1).
	struct ResourceNodeLootEntry
	{
		uint32_t itemId = 0;
		uint32_t minQuantity = 1;
		uint32_t maxQuantity = 1;
		/// Relative weight for random item selection.
		uint32_t weight = 100;
	};

	/// Data-driven resource node type definition loaded from `gathering/nodes.json` (M36.1).
	struct ResourceNodeDefinition
	{
		/// Stable string key: "ore_vein", "herb", "tree", "animal_corpse".
		std::string typeId;
		/// Replication archetype sent to clients for visual representation.
		uint32_t archetypeId = 0;
		/// Harvest cast-bar duration in seconds (2–5 s).
		float harvestTimeSec = 3.0f;
		/// Respawn delay in seconds after depletion (300–900 s = 5–15 min).
		uint32_t respawnTimeSec = 600;
		/// Minimum number of item stacks granted per completed harvest.
		uint32_t minItems = 1;
		/// Maximum number of item stacks granted per completed harvest.
		uint32_t maxItems = 3;
		/// Weighted loot pool rolled on harvest completion.
		std::vector<ResourceNodeLootEntry> lootTable;
	};

	/// Authoritative runtime state of one placed resource node instance (M36.1).
	enum class ResourceNodeState : uint8_t
	{
		/// Node is visible and ready for interaction.
		Available = 0,
		/// A player is currently casting the harvest action.
		Harvesting = 1,
		/// Node has been harvested; awaiting respawn timer.
		Depleted = 2
	};

	/// One resource node instance placed in a zone (M36.1).
	struct ResourceNodeInstance
	{
		/// Stable server-side identifier (globally unique across zones).
		uint32_t instanceId = 0;
		/// References ResourceNodeDefinition::typeId.
		std::string typeId;
		uint32_t zoneId = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		ResourceNodeState state = ResourceNodeState::Available;
		/// clientId of the player currently harvesting (valid only when Harvesting).
		uint32_t harvesterClientId = 0;
		/// Server tick when harvest cast started.
		uint32_t harvestStartTick = 0;
		/// Harvester X position at cast start (used for movement interruption).
		float harvesterStartX = 0.0f;
		/// Harvester Z position at cast start (used for movement interruption).
		float harvesterStartZ = 0.0f;
		/// Server tick at which the node transitions back to Available.
		uint32_t respawnAtTick = 0;
		/// Last progress percent sent to the harvesting client (avoids redundant packets).
		uint8_t lastSentProgressPercent = 255;
	};

	/// One completed harvest payload returned by ResourceNodeRuntime::Tick (M36.1).
	struct ResourceHarvestResult
	{
		uint32_t instanceId = 0;
		uint32_t clientId = 0;
		std::vector<ItemStack> items;
	};

	/// Server-side resource node manager: type definitions + zone instances + runtime state (M36.1).
	class ResourceNodeRuntime final
	{
	public:
		/// Capture the config used to resolve content paths.
		explicit ResourceNodeRuntime(const engine::core::Config& config);

		ResourceNodeRuntime(const ResourceNodeRuntime&) = delete;
		ResourceNodeRuntime& operator=(const ResourceNodeRuntime&) = delete;

		~ResourceNodeRuntime();

		/// Load node type definitions (gathering/nodes.json) and all zone instances
		/// (zones/*/gathering_nodes.json).  Non-fatal when no zone nodes exist.
		bool Init();

		/// Release all state and emit shutdown log.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		/// Return all resource node instances (read-only).
		const std::vector<ResourceNodeInstance>& GetInstances() const { return m_instances; }

		/// Return all resource node instances (mutable, for progress tracking).
		std::vector<ResourceNodeInstance>& GetMutableInstances() { return m_instances; }

		/// Find one node instance by stable id, or nullptr.
		ResourceNodeInstance* FindInstance(uint32_t instanceId);
		const ResourceNodeInstance* FindInstance(uint32_t instanceId) const;

		/// Return the type definition for \p typeId, or nullptr when not found.
		const ResourceNodeDefinition* FindDefinition(std::string_view typeId) const;

		/// Return the active harvesting instanceId for \p clientId, or 0 when not harvesting.
		uint32_t FindHarvestingInstanceId(uint32_t clientId) const;

		/// Begin harvesting: transition Available → Harvesting.
		/// Returns false when the node does not exist, is not Available, or already being harvested.
		bool StartHarvest(
			uint32_t instanceId,
			uint32_t clientId,
			uint32_t currentTick,
			float clientX,
			float clientZ);

		/// Cancel an in-progress harvest and restore the node to Available.
		/// Returns true when a harvest was cancelled (false if the node was not Harvesting).
		bool CancelHarvest(uint32_t instanceId, std::string_view reason);

		/// Cancel all active harvests belonging to \p clientId (e.g. on disconnect).
		void CancelHarvestsForClient(uint32_t clientId, std::string_view reason);

		/// Advance all node timers for one server tick.
		/// Completed harvests are appended to \p outResults for the ServerApp to apply.
		void Tick(
			uint32_t currentTick,
			uint16_t tickHz,
			std::vector<ResourceHarvestResult>& outResults);

		/// Compute harvest progress (0–100) for one Harvesting node at the current tick.
		/// Returns 0 for non-Harvesting nodes or when the definition is missing.
		uint8_t GetProgressPercent(
			uint32_t instanceId,
			uint32_t currentTick,
			uint16_t tickHz) const;

	private:
		/// Load type definitions from `gathering/nodes.json`.
		bool LoadDefinitions();

		/// Load zone-specific node instances from `zones/<zoneName>/gathering_nodes.json`
		/// for every available zone directory.
		void LoadAllZoneNodes();

		/// Roll the loot table of \p def and return the granted item stacks.
		std::vector<ItemStack> RollLoot(const ResourceNodeDefinition& def);

		engine::core::Config m_config;
		std::vector<ResourceNodeDefinition> m_definitions;
		std::vector<ResourceNodeInstance> m_instances;
		std::mt19937 m_rng;
		bool m_initialized = false;
	};
}
