/**
 * @file ChunkStats.cpp
 * @brief Per-chunk/per-ring stats and budget cvars (M09.2).
 */

#include "engine/world/ChunkStats.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <cstring>

namespace engine::world {

size_t ChunkStats::RingIndex(RingType ring) {
    switch (ring) {
        case RingType::Active:  return 0;
        case RingType::Visible: return 1;
        case RingType::Far:     return 2;
    }
    return 0;
}

void ChunkStats::BeginFrame() {
    std::memset(m_ringStats, 0, sizeof(m_ringStats));
    m_chunkStats.clear();
}

void ChunkStats::RecordDraw(const ChunkCoord& chunk, RingType ring, uint32_t drawcalls, uint32_t triangles) {
    const size_t ri = RingIndex(ring);
    m_ringStats[ri].drawcalls += drawcalls;
    m_ringStats[ri].triangles += triangles;
    const uint64_t key = ChunkKey(chunk);
    ChunkStatEntry& e = m_chunkStats[key];
    e.drawcalls += drawcalls;
    e.triangles += triangles;
}

RingStats ChunkStats::GetRingStats(RingType ring) const {
    return m_ringStats[RingIndex(ring)];
}

uint32_t ChunkStats::GetBudgetDrawcalls(RingType ring) const {
    switch (ring) {
        case RingType::Active:
            return static_cast<uint32_t>(engine::core::Config::GetInt("world.budget_active_drawcalls", 1000));
        case RingType::Visible:
            return static_cast<uint32_t>(engine::core::Config::GetInt("world.budget_visible_drawcalls", 300));
        case RingType::Far:
            return static_cast<uint32_t>(engine::core::Config::GetInt("world.budget_far_drawcalls", 50));
    }
    return 1000u;
}

void ChunkStats::GetChunkStats(std::vector<std::pair<ChunkCoord, ChunkStatEntry>>& out) const {
    out.clear();
    out.reserve(m_chunkStats.size());
    for (const auto& [coord, entry] : m_chunkStats)
        out.emplace_back(coord, entry);
}

void ChunkStats::LogFrameStats() const {
    const char* ringNames[] = { "Active", "Visible", "Far" };
    for (size_t i = 0; i < kRingCount; ++i) {
        const RingType r = static_cast<RingType>(i);
        const RingStats s = m_ringStats[i];
        const uint32_t budget = GetBudgetDrawcalls(r);
        LOG_INFO(Render,
            "ChunkStats {}: drawcalls={} (budget {}), triangles={}",
            ringNames[i], s.drawcalls, budget, s.triangles);
    }
}

} // namespace engine::world
