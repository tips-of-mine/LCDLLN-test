#pragma once

/** @file SpawnerDef.h — Spawner JSON schema (M15.2). */
#include <cstdint>
#include <string>
#include <vector>

namespace engine::spawner {

struct SpawnerDef {
    int32_t zoneId = 0;
    float position[3] = {0.f, 0.f, 0.f};
    uint32_t archetypeId = 1u;
    uint32_t count = 1u;
    float respawnSec = 10.f;
    int32_t activationRadiusCells = 1;
};

bool LoadSpawnersJson(const std::string& path, std::vector<SpawnerDef>& out);

} // namespace engine::spawner
