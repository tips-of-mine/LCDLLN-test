/**
 * @file World.cpp
 * @brief World model: Zone/Chunk and ring chunk lists (M09.1).
 */

#include "engine/world/World.h"

#include <cmath>

namespace engine::world {

ChunkCoord WorldToChunkCoord(float worldX, float worldZ) {
    const float zoneXf = std::floor(worldX / static_cast<float>(kZoneSize));
    const float zoneZf = std::floor(worldZ / static_cast<float>(kZoneSize));
    const int32_t zoneX = static_cast<int32_t>(zoneXf);
    const int32_t zoneZ = static_cast<int32_t>(zoneZf);
    const float localX = worldX - zoneX * kZoneSize;
    const float localZ = worldZ - zoneZ * kZoneSize;
    const int32_t chunkX = static_cast<int32_t>(std::floor(localX / static_cast<float>(kChunkSize)));
    const int32_t chunkZ = static_cast<int32_t>(std::floor(localZ / static_cast<float>(kChunkSize)));
    ChunkCoord c;
    c.zoneX = zoneX;
    c.zoneZ = zoneZ;
    c.chunkX = chunkX;
    c.chunkZ = chunkZ;
    if (c.chunkX >= kChunksPerZone) c.chunkX = kChunksPerZone - 1;
    if (c.chunkZ >= kChunksPerZone) c.chunkZ = kChunksPerZone - 1;
    if (c.chunkX < 0) c.chunkX = 0;
    if (c.chunkZ < 0) c.chunkZ = 0;
    return c;
}

ChunkBoundsResult ChunkBounds(const ChunkCoord& c) {
    const float baseX = static_cast<float>(c.zoneX * kZoneSize + c.chunkX * kChunkSize);
    const float baseZ = static_cast<float>(c.zoneZ * kZoneSize + c.chunkZ * kChunkSize);
    ChunkBoundsResult b;
    b.minX = baseX;
    b.minZ = baseZ;
    b.maxX = baseX + static_cast<float>(kChunkSize);
    b.maxZ = baseZ + static_cast<float>(kChunkSize);
    return b;
}

int32_t RingRadiusChunks(RingType ring) {
    switch (ring) {
        case RingType::Active: return 2;   // 5x5
        case RingType::Visible: return 3;  // 7x7
        case RingType::Far: return 4;       // 9x9 HLOD
    }
    return 2;
}

namespace {

ChunkCoord GlobalChunkToCoord(int32_t gx, int32_t gz) {
    ChunkCoord c;
    int32_t zx = gx / kChunksPerZone;
    int32_t zz = gz / kChunksPerZone;
    int32_t cx = gx % kChunksPerZone;
    int32_t cz = gz % kChunksPerZone;
    if (cx < 0) { cx += kChunksPerZone; zx -= 1; }
    if (cz < 0) { cz += kChunksPerZone; zz -= 1; }
    c.zoneX = zx;
    c.zoneZ = zz;
    c.chunkX = cx;
    c.chunkZ = cz;
    return c;
}

} // namespace

void GetChunksForRing(const ChunkCoord& center, RingType ring, std::vector<ChunkCoord>& out) {
    const int32_t radius = RingRadiusChunks(ring);
    const int32_t gxCenter = center.zoneX * kChunksPerZone + center.chunkX;
    const int32_t gzCenter = center.zoneZ * kChunksPerZone + center.chunkZ;
    for (int32_t gz = gzCenter - radius; gz <= gzCenter + radius; ++gz) {
        for (int32_t gx = gxCenter - radius; gx <= gxCenter + radius; ++gx) {
            out.push_back(GlobalChunkToCoord(gx, gz));
        }
    }
}

void EmitChunkRequestsForPosition(float worldX, float worldZ, const ChunkRequestCallback& callback) {
    if (!callback) return;
    const ChunkCoord center = WorldToChunkCoord(worldX, worldZ);
    std::vector<ChunkCoord> chunks;
    chunks.reserve(81);
    for (RingType r : { RingType::Active, RingType::Visible, RingType::Far }) {
        chunks.clear();
        GetChunksForRing(center, r, chunks);
        callback(r, chunks);
    }
}

} // namespace engine::world
