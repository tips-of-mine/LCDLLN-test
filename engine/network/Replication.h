#pragma once

/**
 * @file Replication.h
 * @brief Spawn/Despawn messages and snapshot entity states; serialization (M13.3). No delta compression.
 */

#include "engine/network/Protocol.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace engine::network {

/** @brief Minimal entity state for replication (position, rotation Y, velocity). */
struct ReplicationEntityState {
    uint64_t entityId = 0;
    uint32_t archetypeId = 0;
    float position[3] = {0.f, 0.f, 0.f};
    float rotationY = 0.f;
    float velocity[3] = {0.f, 0.f, 0.f};
};

/** @brief Serializes a Spawn message into outBuffer. Returns bytes written. */
size_t SerializeSpawn(uint64_t entityId, uint32_t archetypeId,
                     const float position[3], float rotationY, const float velocity[3],
                     std::vector<uint8_t>& outBuffer);

/** @brief Serializes a Despawn message into outBuffer. Returns bytes written. */
size_t SerializeDespawn(uint64_t entityId, std::vector<uint8_t>& outBuffer);

/** @brief Serializes a Snapshot message with entity states (tick + list of states). Returns bytes written. */
size_t SerializeSnapshotWithStates(uint32_t tick, const std::vector<ReplicationEntityState>& states,
                                   std::vector<uint8_t>& outBuffer);

/** @brief Parses a Spawn message (type already consumed). Returns true if buffer size is valid. */
bool ParseSpawn(const uint8_t* data, size_t size, ReplicationEntityState& out);

/** @brief Parses a Despawn message (type already consumed). Returns true if size >= 8. */
bool ParseDespawn(const uint8_t* data, size_t size, uint64_t& outEntityId);

/** @brief Parses a Snapshot message with entity states (type already consumed). Returns true if valid. */
bool ParseSnapshotWithStates(const uint8_t* data, size_t size, uint32_t& outTick, std::vector<ReplicationEntityState>& outStates);

/** @brief Callback: get entity ids in a cell (zone and cell indices). Return 64-bit ids for replication. */
using GetEntityIdsInCellFn = std::function<std::vector<uint64_t>(int32_t zoneX, int32_t zoneZ, int32_t cellX, int32_t cellZ)>;
/** @brief Callback: get entity state; return false if entity unknown. */
using GetEntityStateFn = std::function<bool(uint64_t entityId, ReplicationEntityState& out)>;

/**
 * @brief Builds replication messages for one client: Spawn for cells entered, Despawn for cells left, Snapshot with states for current cells.
 *
 * @param cellsEntered  Cells that entered the client's interest set this update.
 * @param cellsLeft    Cells that left the client's interest set.
 * @param currentCells Current interest cells (for snapshot entity states).
 * @param getEntityIdsInCell Callback to get entity ids in a cell.
 * @param getEntityState     Callback to get entity state by id.
 * @param outSpawns    Serialized Spawn messages (one per entity in cellsEntered).
 * @param outDespawns  Serialized Despawn messages (one per entity that was only in cellsLeft).
 * @param outSnapshot Serialized Snapshot with states for entities in currentCells.
 * @param snapshotTick Tick number to write in the snapshot (e.g. server tick).
 */
void BuildReplicationForClient(
    uint32_t snapshotTick,
    const std::vector<std::array<int32_t, 4>>& cellsEntered,
    const std::vector<std::array<int32_t, 4>>& cellsLeft,
    const std::vector<std::array<int32_t, 4>>& currentCells,
    GetEntityIdsInCellFn getEntityIdsInCell,
    GetEntityStateFn getEntityState,
    std::vector<std::vector<uint8_t>>& outSpawns,
    std::vector<std::vector<uint8_t>>& outDespawns,
    std::vector<uint8_t>& outSnapshot);

} // namespace engine::network
