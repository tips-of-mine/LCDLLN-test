#pragma once

/**
 * @file VolumeFormat.h
 * @brief Read/write gameplay volumes to JSON for layout export (M12.3).
 */

#include "engine/world/GameplayVolume.h"

#include <string>
#include <vector>

namespace engine::world {

/**
 * @brief Loads volumes from a JSON file (e.g. zone base path + "/volumes.json").
 *
 * @param path    Full path to volumes.json (content-relative resolved by caller).
 * @param out     Filled with volume entities.
 * @return        true on success; false on missing file or parse error.
 */
bool ReadVolumesJson(const std::string& path, std::vector<GameplayVolume>& out);

/**
 * @brief Writes volumes to a JSON file (export dans layout).
 *
 * @param path    Full path to volumes.json.
 * @param volumes Volume entities to write.
 * @return        true on success.
 */
bool WriteVolumesJson(const std::string& path, const std::vector<GameplayVolume>& volumes);

/**
 * @brief Returns true if world position (x,y,z) is inside the volume (M13.4 zone transition detection).
 *
 * Box: AABB test; Sphere: distance from center <= radius.
 */
bool PointInVolume(float x, float y, float z, const GameplayVolume& vol);

} // namespace engine::world
