#pragma once

/**
 * @file ChunkStats.h
 * @brief Per-chunk/per-ring drawcall and triangle counters + budget cvars (M09.2).
 *
 * Instrumentation: tag draws with chunk id and ring; accumulate stats; display via log or overlay.
 */

#include "engine/world/World.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::world {

/** @brief Hash for ChunkCoord for use in unordered_map. */
struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& c) const noexcept {
        std::size_t h = 0;
        h ^= static_cast<size_t>(static_cast<uint32_t>(c.zoneX)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(static_cast<uint32_t>(c.zoneZ)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(static_cast<uint32_t>(c.chunkX)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(static_cast<uint32_t>(c.chunkZ)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

/** @brief Stats for one ring (drawcalls + triangles). */
struct RingStats {
    uint32_t drawcalls = 0;
    uint32_t triangles = 0;
};

/** @brief Stats for one chunk (drawcalls + triangles). */
struct ChunkStatEntry {
    uint32_t drawcalls = 0;
    uint32_t triangles = 0;
};

/**
 * @brief Accumulates drawcalls and triangles per chunk and per ring; exposes budget cvars.
 *
 * Call BeginFrame() at start of frame, RecordDraw() for each tagged draw, then
 * LogFrameStats() or GetRingStats()/GetChunkStats() for display.
 */
class ChunkStats {
public:
    ChunkStats() = default;

    /**
     * @brief Clears per-frame counters. Call at start of each frame.
     */
    void BeginFrame();

    /**
     * @brief Records a draw tagged with chunk and ring (e.g. from geometry pass).
     *
     * @param chunk    Chunk this draw belongs to.
     * @param ring     Ring type (Active, Visible, Far).
     * @param drawcalls Number of draw calls (typically 1 per call).
     * @param triangles Triangle count for this draw.
     */
    void RecordDraw(const ChunkCoord& chunk, RingType ring, uint32_t drawcalls, uint32_t triangles);

    /**
     * @brief Returns accumulated stats for a ring (Active, Visible, Far).
     */
    [[nodiscard]] RingStats GetRingStats(RingType ring) const;

    /**
     * @brief Returns budget (target max drawcalls) for a ring from config cvars.
     * Keys: world.budget_active_drawcalls (1000), world.budget_visible_drawcalls (300), world.budget_far_drawcalls (50).
     */
    [[nodiscard]] uint32_t GetBudgetDrawcalls(RingType ring) const;

    /**
     * @brief Fills a snapshot of per-chunk stats (for overlay or debug).
     */
    void GetChunkStats(std::vector<std::pair<ChunkCoord, ChunkStatEntry>>& out) const;

    /**
     * @brief Logs per-ring stats and budget comparison to engine log (debug).
     */
    void LogFrameStats() const;

private:
    static constexpr size_t kRingCount = 3;
    RingStats m_ringStats[kRingCount]{};
    std::unordered_map<ChunkCoord, ChunkStatEntry, ChunkCoordHash> m_chunkStats;

    static size_t RingIndex(RingType ring);
};

} // namespace engine::world
