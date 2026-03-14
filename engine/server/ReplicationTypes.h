#pragma once

#include <cstdint>

namespace engine::server
{
	/// Stable 64-bit entity identifier required by the replication protocol.
	using EntityId = uint64_t;

	/// Replicated state flag set when an entity reached 0 HP.
	inline constexpr uint32_t kEntityStateDead = 1u << 0;

	/// Minimal authoritative stats component shared by players and mobs.
	struct StatsComponent
	{
		uint32_t currentHealth = 0;
		uint32_t maxHealth = 0;
	};

	/// Minimal combat component used by the authoritative attack validation.
	struct CombatComponent
	{
		uint32_t damagePerHit = 0;
		float attackRangeMeters = 0.0f;
		uint32_t cooldownTicks = 0;
		uint32_t nextAttackTick = 0;
	};

	/// Minimal replicated entity state shared by spawn and snapshot messages.
	struct EntityState
	{
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		float yawRadians = 0.0f;
		float velocityX = 0.0f;
		float velocityY = 0.0f;
		float velocityZ = 0.0f;
		uint32_t currentHealth = 0;
		uint32_t maxHealth = 0;
		uint32_t stateFlags = 0;
	};

	/// Spawn payload for one entity entering a client's interest set.
	struct SpawnEntity
	{
		EntityId entityId = 0;
		uint32_t archetypeId = 0;
		EntityState state{};
	};

	/// Despawn payload for one entity leaving a client's interest set.
	struct DespawnEntity
	{
		EntityId entityId = 0;
	};

	/// Snapshot payload for one already-spawned entity state update.
	struct SnapshotEntity
	{
		EntityId entityId = 0;
		EntityState state{};
	};
}
