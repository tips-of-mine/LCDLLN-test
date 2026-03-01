#pragma once

/**
 * @file MobAiUpdate.h
 * @brief Mob AI update: state transitions, move toward target or spawn, leash/return (M14.2).
 */

#include "engine/ai/AiState.h"
#include "engine/ai/ThreatTable.h"

#include <cstdint>
#include <functional>

namespace engine::ai {

/** @brief Callback: get entity position (x, z). Returns true if entity found. */
using GetEntityPositionFn = std::function<bool(uint32_t entityId, float& outX, float& outZ)>;

/**
 * @brief Updates one mob's AI state and position. Call at ~10 Hz (e.g. every kAiUpdateTickStep ticks).
 *
 * Transitions: Idle/Patrol -> Aggro when threat table has a target. Aggro -> Return when distance to spawn > leash.
 * Return -> Idle when back at spawn. In Aggro: move toward target. In Return: move toward spawn.
 *
 * @param mobEntityId    Mob entity id.
 * @param state          Current state (updated in place).
 * @param spawnPos       Spawn position [x, y, z].
 * @param currentPos    Current position [x, y, z] (updated in place).
 * @param threatTable    Threat table (cleared when entering Return).
 * @param leashDistance  Max distance from spawn before forcing Return.
 * @param getEntityPos   Callback to get entity position by id.
 * @param moveSpeed      Movement speed in m/s.
 * @param dt             Delta time in seconds for this update.
 */
void UpdateMobAi(uint32_t mobEntityId,
                 MobAiState& state,
                 const float spawnPos[3],
                 float currentPos[3],
                 ThreatTable& threatTable,
                 float leashDistance,
                 GetEntityPositionFn getEntityPos,
                 float moveSpeed,
                 float dt);

/** @brief Returns true if distance from (x,z) to spawn (sx,sz) is within leash. */
bool IsWithinLeash(float x, float z, float sx, float sz, float leashDistance);

} // namespace engine::ai
