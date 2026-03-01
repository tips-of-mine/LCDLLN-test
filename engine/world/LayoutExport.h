#pragma once

/**
 * @file LayoutExport.h
 * @brief Export layout.json versionné (instances + volumes), tri stable pour diff git (M12.4).
 */

#include "engine/world/GameplayVolume.h"
#include "engine/world/ZoneBuildFormat.h"

#include <string>
#include <vector>

namespace engine::world {

/** @brief Layout format version for layout.json (M12.4). */
constexpr uint32_t kLayoutJsonVersion = 1u;

/**
 * @brief Writes layout.json with version, instances (sorted by assetId then index), volumes (sorted by type then index).
 *
 * Schema: version, instances (guid, position, mesh?, material?, assetId?), volumes (type, shape, position, ...).
 * Stable ordering for git diff. Builder consumes version + instances; optional assetId used when present.
 *
 * @param path      Full path to layout.json.
 * @param instances Zone chunk instances.
 * @param volumes   Gameplay volumes; may be empty.
 * @return          true on success.
 */
bool WriteLayoutJson(const std::string& path,
                    const std::vector<ZoneChunkInstance>& instances,
                    const std::vector<GameplayVolume>& volumes);

} // namespace engine::world
