/**
 * @file HlodRuntime.cpp
 * @brief HLOD distance threshold and CPU culling helpers (M09.5).
 */

#include "engine/world/HlodRuntime.h"
#include "engine/core/Config.h"

#include <cmath>

namespace engine::world {

float GetHlodDistanceThreshold() {
    return ::engine::core::Config::GetFloat("world.hlod_distance_threshold", 200.f);
}

bool UseHlodForDistance(float distance) {
    return distance >= GetHlodDistanceThreshold();
}

bool VisibleInFrustum(const ::engine::math::Frustum& frustum, const float aabbMin[3], const float aabbMax[3]) {
    return ::engine::math::TestAABB(frustum, aabbMin, aabbMax);
}

bool WithinDrawDistance(const float cameraPos[3], const float point[3], float maxDistance) {
    float dx = point[0] - cameraPos[0];
    float dy = point[1] - cameraPos[1];
    float dz = point[2] - cameraPos[2];
    float dSq = dx * dx + dy * dy + dz * dz;
    return dSq <= (maxDistance * maxDistance);
}

} // namespace engine::world
