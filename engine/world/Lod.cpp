/**
 * @file Lod.cpp
 * @brief LOD selection by distance (M09.3).
 */

#include "engine/world/Lod.h"
#include "engine/core/Config.h"

namespace engine::world {

unsigned SelectLod(float distance) {
    const float max0 = static_cast<float>(engine::core::Config::GetFloat("world.lod_max_0", 25.f));
    const float max1 = static_cast<float>(engine::core::Config::GetFloat("world.lod_max_1", 60.f));
    const float max2 = static_cast<float>(engine::core::Config::GetFloat("world.lod_max_2", 150.f));
    const float max3 = static_cast<float>(engine::core::Config::GetFloat("world.lod_max_3", 400.f));
    if (distance < max0) return 0u;
    if (distance < max1) return 1u;
    if (distance < max2) return 2u;
    if (distance < max3) return 3u;
    return 3u;
}

} // namespace engine::world
