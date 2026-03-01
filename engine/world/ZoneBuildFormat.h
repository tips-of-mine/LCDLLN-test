#pragma once

/**
 * @file ZoneBuildFormat.h
 * @brief Reader for zone_builder output: zone.meta, chunk.meta (bounds+flags), instances.bin (M11.2, M11.5 versioned).
 */

#include "engine/world/VersionedHeader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::world {

constexpr uint32_t kZoneMetaMagic = 0x4D4E4F5Au;
constexpr uint32_t kZoneChunkMetaMagic = 0x4D4E4843u;
/** @brief instances.bin file magic "INST" (M11.5). */
constexpr uint32_t kInstancesBinMagic = 0x54534E49u;

struct ZoneChunkMeta {
    float boundsMin[3] = { 0.f, 0.f, 0.f };
    float boundsMax[3] = { 0.f, 0.f, 0.f };
    uint32_t flags = 0;
};

struct ZoneChunkInstance {
    float transform[16] = {};
    uint32_t assetId = 0;
    uint32_t flags = 0;
};

bool ReadZoneMeta(const std::string& path, int32_t& zoneId, std::vector<std::pair<int32_t, int32_t>>& chunkCoords);
bool ReadZoneChunkMeta(const std::string& path, ZoneChunkMeta& out);
bool ReadZoneChunkInstances(const std::string& path, std::vector<ZoneChunkInstance>& out);

/**
 * @brief Writes instances.bin with versioned header (M12.2 editor save).
 *
 * @param path     Output path (e.g. chunkDir + "/instances.bin").
 * @param instances Instance data to write.
 * @param vh       Versioned header (formatVersion, builderVersion, engineVersion, contentHash).
 * @return         true on success.
 */
bool WriteZoneChunkInstances(const std::string& path, const std::vector<ZoneChunkInstance>& instances, const VersionedHeader& vh);

} // namespace engine::world
