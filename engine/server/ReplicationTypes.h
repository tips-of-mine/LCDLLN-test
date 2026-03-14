#pragma once

#include <cstdint>

namespace engine::server
{
	/// Stable 64-bit entity identifier required by the replication protocol.
	using EntityId = uint64_t;

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
