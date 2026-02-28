#pragma once

/**
 * @file VersionedHeader.h
 * @brief Common versioned header for bin/pak/meta outputs (M11.5).
 *
 * Layout after magic: formatVersion, builderVersion, engineVersion, contentHash (xxhash).
 * Runtime refuses to load if version incompatible (log clear).
 */

#include <cstdint>

namespace engine::world {

/** @brief Size of the versioned block after magic (formatVersion + builderVersion + engineVersion + contentHash). */
constexpr size_t kVersionedHeaderSize = 4u + 4u + 4u + 8u;

/** @brief Format version for zone build files (bin/pak/meta). */
constexpr uint32_t kZoneBuildFormatVersion = 1u;

/** @brief Minimum engine version that can load zone build outputs; builder writes this. */
constexpr uint32_t kCurrentEngineVersion = 1u;

/** @brief Builder version written by zone_builder (and other tools). */
constexpr uint32_t kCurrentBuilderVersion = 1u;

/**
 * @brief Versioned header written after magic in each zone build binary (M11.5).
 */
struct VersionedHeader {
    uint32_t formatVersion = kZoneBuildFormatVersion;
    uint32_t builderVersion = kCurrentBuilderVersion;
    uint32_t engineVersion = kCurrentEngineVersion;
    uint64_t contentHash = 0u;
};

/**
 * @brief Validates versioned header for compatibility. Returns true if load is allowed.
 * Logs error and returns false if formatVersion or engineVersion is incompatible.
 */
bool ValidateVersionedHeader(const VersionedHeader& h, const char* fileType);

} // namespace engine::world
