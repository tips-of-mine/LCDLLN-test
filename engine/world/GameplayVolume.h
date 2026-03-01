#pragma once

/**
 * @file GameplayVolume.h
 * @brief Volume entities for triggers, spawns, zone transitions (M12.3).
 *
 * Box or sphere shapes; types: trigger, spawnArea, zoneTransition.
 */

#include <cstdint>
#include <string>

namespace engine::world {

/** @brief Volume usage type (M12.3). */
enum class VolumeType : uint8_t {
    Trigger = 0,
    SpawnArea = 1,
    ZoneTransition = 2,
};

/** @brief Volume shape. */
enum class VolumeShape : uint8_t {
    Box = 0,
    Sphere = 1,
};

/**
 * @brief One gameplay volume (trigger, spawn area, or zone transition).
 *
 * For Box: halfExtents[3] used; radius ignored.
 * For Sphere: radius used; halfExtents unused.
 * actionId: string ID for script/target (e.g. zone ID for zoneTransition).
 */
struct GameplayVolume {
    VolumeType type = VolumeType::Trigger;
    VolumeShape shape = VolumeShape::Box;
    float position[3] = {0.f, 0.f, 0.f};
    float halfExtents[3] = {2.f, 2.f, 2.f};  ///< Box half-extents (world units).
    float radius = 2.f;                       ///< Sphere radius (world units).
    std::string actionId;                    ///< Optional action/script ID (e.g. target zone).
};

} // namespace engine::world
