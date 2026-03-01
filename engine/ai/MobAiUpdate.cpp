/**
 * @file MobAiUpdate.cpp
 * @brief Mob AI update implementation: state machine, leash, move toward target/spawn (M14.2).
 */

#include "engine/ai/MobAiUpdate.h"
#include <cmath>

namespace engine::ai {

namespace {
constexpr float kAtSpawnEpsilon = 0.5f;
}

bool IsWithinLeash(float x, float z, float sx, float sz, float leashDistance) {
    float dx = x - sx, dz = z - sz;
    return (dx * dx + dz * dz) <= (leashDistance * leashDistance);
}

static void MoveToward(float& curX, float& curZ, float targetX, float targetZ, float maxDist) {
    float dx = targetX - curX, dz = targetZ - curZ;
    float d = std::sqrt(dx * dx + dz * dz);
    if (d <= 1e-6f) return;
    float step = (maxDist < d) ? maxDist : d;
    curX += (dx / d) * step;
    curZ += (dz / d) * step;
}

static float DistSq(float x, float z, float ox, float oz) {
    float dx = x - ox, dz = z - oz;
    return dx * dx + dz * dz;
}

void UpdateMobAi(uint32_t mobEntityId,
                 MobAiState& state,
                 const float spawnPos[3],
                 float currentPos[3],
                 ThreatTable& threatTable,
                 float leashDistance,
                 GetEntityPositionFn getEntityPos,
                 float moveSpeed,
                 float dt) {
    const float maxMove = moveSpeed * dt;
    const float spawnX = spawnPos[0], spawnZ = spawnPos[2];
    const float curX = currentPos[0], curZ = currentPos[2];

    switch (state) {
    case MobAiState::Idle:
    case MobAiState::Patrol: {
        uint32_t targetId = threatTable.GetTarget(mobEntityId);
        if (targetId != 0xFFFFFFFFu)
            state = MobAiState::Aggro;
        break;
    }
    case MobAiState::Aggro: {
        if (!IsWithinLeash(curX, curZ, spawnX, spawnZ, leashDistance)) {
            state = MobAiState::Return;
            threatTable.Clear(mobEntityId);
            MoveToward(currentPos[0], currentPos[2], spawnX, spawnZ, maxMove);
            break;
        }
        uint32_t targetId = threatTable.GetTarget(mobEntityId);
        if (targetId == 0xFFFFFFFFu) {
            state = MobAiState::Idle;
            break;
        }
        float tx = 0.f, tz = 0.f;
        if (getEntityPos && getEntityPos(targetId, tx, tz))
            MoveToward(currentPos[0], currentPos[2], tx, tz, maxMove);
        break;
    }
    case MobAiState::Return: {
        MoveToward(currentPos[0], currentPos[2], spawnX, spawnZ, maxMove);
        if (DistSq(currentPos[0], currentPos[2], spawnX, spawnZ) < kAtSpawnEpsilon * kAtSpawnEpsilon)
            state = MobAiState::Idle;
        break;
    }
    }
}

} // namespace engine::ai
