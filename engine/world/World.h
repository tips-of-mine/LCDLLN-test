#pragma once

/**
 * @file World.h
 * @brief World model: Zone 4km, Chunk 256m, rings (Active 5x5, Visible 7x7, Far HLOD).
 *
 * Ticket: M09.1 — World model: Zone 4km, Chunk 256m, rings.
 * Unités en mètres; origine locale par zone.
 */

#include <cstdint>
#include <functional>
#include <vector>

namespace engine::world {

/** @brief Zone size in meters (4 km). */
constexpr int32_t kZoneSize = 4096;

/** @brief Chunk size in meters (256 m). */
constexpr int32_t kChunkSize = 256;

/** @brief Chunks per zone axis (ZoneSize / ChunkSize). */
constexpr int32_t kChunksPerZone = kZoneSize / kChunkSize;

/**
 * @brief World model: holds zone/chunk sizing (meters, local origin per zone).
 */
struct World {
    static constexpr int32_t ZoneSize = kZoneSize;
    static constexpr int32_t ChunkSize = kChunkSize;
};

/**
 * @brief Chunk identifier: zone indices + local chunk indices within zone (0..kChunksPerZone-1). "Chunk" in ticket.
 */
struct ChunkCoord {
    int32_t zoneX = 0;
    int32_t zoneZ = 0;
    int32_t chunkX = 0; ///< Local chunk index in zone, [0, kChunksPerZone).
    int32_t chunkZ = 0; ///< Local chunk index in zone, [0, kChunksPerZone).

    [[nodiscard]] bool operator==(const ChunkCoord& o) const noexcept {
        return zoneX == o.zoneX && zoneZ == o.zoneZ && chunkX == o.chunkX && chunkZ == o.chunkZ;
    }
};

/**
 * @brief Zone identifier (origin in zone-local space is 0,0). "Zone" in ticket.
 */
struct ZoneCoord {
    int32_t zoneX = 0;
    int32_t zoneZ = 0;

    [[nodiscard]] bool operator==(const ZoneCoord& o) const noexcept {
        return zoneX == o.zoneX && zoneZ == o.zoneZ;
    }
};

/**
 * @brief World-space axis-aligned bounds in meters (min/max X and Z).
 */
struct ChunkBoundsResult {
    float minX = 0.f;
    float minZ = 0.f;
    float maxX = 0.f;
    float maxZ = 0.f;
};

/** @brief Ring type for chunk requirement (Active 5x5, Visible 7x7, Far HLOD). */
enum class RingType : uint8_t {
    Active,  ///< 5x5 chunks
    Visible, ///< 7x7 chunks
    Far,     ///< Far HLOD ring (9x9)
};

/**
 * @brief Converts world position (meters) to chunk coordinates.
 *
 * @param worldX World X in meters.
 * @param worldZ World Z in meters.
 * @return ChunkCoord for the chunk containing (worldX, worldZ).
 */
ChunkCoord WorldToChunkCoord(float worldX, float worldZ);

/**
 * @brief Returns world-space bounds (meters) of the given chunk.
 *
 * @param c Chunk coordinates.
 * @return Bounds in world meters [minX, maxX) x [minZ, maxZ).
 */
ChunkBoundsResult ChunkBounds(const ChunkCoord& c);

/**
 * @brief Returns the chunk radius (half-side) in chunks for a ring type.
 * Active=2 (5x5), Visible=3 (7x7), Far=4 (9x9).
 */
int32_t RingRadiusChunks(RingType ring);

/**
 * @brief Fills the list of chunk coordinates required for a ring centered on the given chunk.
 *
 * @param center Center chunk (e.g. player chunk).
 * @param ring   Ring type (Active, Visible, Far).
 * @param out    Output list of ChunkCoord (appended, not cleared).
 */
void GetChunksForRing(const ChunkCoord& center, RingType ring, std::vector<ChunkCoord>& out);

/**
 * @brief Callback type for emitting chunk requests to the scheduler (M10).
 * Called with ring type and the list of chunk coords required for that ring.
 */
using ChunkRequestCallback = std::function<void(RingType ring, const std::vector<ChunkCoord>& chunks)>;

/**
 * @brief Computes chunks required for all rings around a world position and invokes the callback per ring.
 *
 * Call from game loop when player/observer position changes; hysteresis (e.g. threshold before re-emitting)
 * can be applied by the caller to avoid thrash at chunk boundaries.
 *
 * @param worldX    Player (or observer) world X in meters.
 * @param worldZ    Player (or observer) world Z in meters.
 * @param callback  Called once per ring (Active, Visible, Far) with the list of chunks for that ring.
 */
void EmitChunkRequestsForPosition(float worldX, float worldZ, const ChunkRequestCallback& callback);

} // namespace engine::world
