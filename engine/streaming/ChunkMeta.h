#pragma once

/**
 * @file ChunkMeta.h
 * @brief chunk.meta format: list of chunk assets (geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin) with sizes (M10.5).
 *
 * Enables loading by priority: geo first, then tex, then instances/nav/probes.
 */

#include <cstdint>
#include <cstddef>
#include <string>

namespace engine::streaming {

/** @brief chunk.meta file magic "META". */
constexpr uint32_t kChunkMetaMagic = 0x4154454Du;

/** @brief chunk.meta format version. */
constexpr uint32_t kChunkMetaVersion = 1u;

/** @brief Number of asset slots in chunk.meta (geo, tex, instances, navmesh, probes). */
constexpr size_t kChunkMetaSlotCount = 5u;

/** @brief Asset type index for chunk.meta slots and load priority (0=geo first). */
enum class ChunkAssetType : uint8_t {
    Geo = 0,
    Tex = 1,
    Instances = 2,
    Nav = 3,
    Probes = 4,
};

/** @brief Returns filename for asset type (e.g. "geo.pak"). */
const char* ChunkAssetFileName(ChunkAssetType t) noexcept;

/**
 * @brief Per-slot info: present flag and size in bytes.
 */
struct ChunkMetaSlot {
    uint8_t  present = 0;
    uint8_t  reserved[7] = {};
    uint64_t size = 0;
};

/**
 * @brief In-memory chunk.meta: which assets exist and their sizes.
 */
struct ChunkMeta {
    ChunkMetaSlot slots[kChunkMetaSlotCount] = {};

    [[nodiscard]] bool HasAsset(ChunkAssetType t) const noexcept;
    [[nodiscard]] uint64_t GetAssetSize(ChunkAssetType t) const noexcept;
};

/**
 * @brief Reads chunk.meta from a file path into out. Returns false on error or invalid format.
 */
bool ReadChunkMeta(const std::string& path, ChunkMeta& out);

} // namespace engine::streaming
