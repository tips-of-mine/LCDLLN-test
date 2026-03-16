#include "engine/world/ChunkBudgetStats.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <cstddef>

namespace engine::world
{
	namespace
	{
		constexpr size_t RingToIndex(ChunkRing r)
		{
			switch (r)
			{
			case ChunkRing::Active:  return 0;
			case ChunkRing::Visible: return 1;
			case ChunkRing::Far:     return 2;
			}
			return 2;
		}
	}

	void ChunkBudgetStats::Init(const engine::core::Config& config)
	{
		m_budgetActiveDrawcalls  = static_cast<uint32_t>(config.GetInt("world.budget_active_drawcalls", 1000));
		m_budgetVisibleDrawcalls = static_cast<uint32_t>(config.GetInt("world.budget_visible_drawcalls", 300));
		m_budgetFarDrawcalls     = static_cast<uint32_t>(config.GetInt("world.budget_far_drawcalls", 50));
		if (engine::core::Log::IsActive()) LOG_INFO(World, "[ChunkBudgetStats] Init OK (active={}, visible={}, far={})", m_budgetActiveDrawcalls, m_budgetVisibleDrawcalls, m_budgetFarDrawcalls);
	}

	void ChunkBudgetStats::RecordDraw(GlobalChunkCoord /*chunkId*/, ChunkRing ring, uint32_t drawcallCount, uint32_t triangleCount)
	{
		const size_t i = RingToIndex(ring);
		if (i < kRingCount)
		{
			m_ringStats[i].drawcalls += drawcallCount;
			m_ringStats[i].triangles += triangleCount;
		}
	}

	void ChunkBudgetStats::ResetPerFrame()
	{
		for (size_t i = 0; i < kRingCount; ++i)
		{
			m_ringStats[i].drawcalls = 0;
			m_ringStats[i].triangles = 0;
		}
	}

	RingStats ChunkBudgetStats::GetRingStats(ChunkRing ring) const
	{
		const size_t i = RingToIndex(ring);
		return i < kRingCount ? m_ringStats[i] : RingStats{};
	}

	uint32_t ChunkBudgetStats::GetBudgetDrawcalls(ChunkRing ring) const
	{
		switch (ring)
		{
		case ChunkRing::Active:  return m_budgetActiveDrawcalls;
		case ChunkRing::Visible: return m_budgetVisibleDrawcalls;
		case ChunkRing::Far:     return m_budgetFarDrawcalls;
		}
		return m_budgetFarDrawcalls;
	}

	void ChunkBudgetStats::LogStats() const
	{
		const auto logRing = [this](const char* name, ChunkRing ring) {
			const RingStats s = GetRingStats(ring);
			const uint32_t budget = GetBudgetDrawcalls(ring);
			const char* over = (budget > 0 && s.drawcalls > budget) ? " OVER" : "";
			if (engine::core::Log::IsActive()) LOG_INFO(World, "M09.2 {}: drawcalls={} (budget {}), triangles={}{}", name, s.drawcalls, budget, s.triangles, over);
		};
		logRing("Active",  ChunkRing::Active);
		logRing("Visible", ChunkRing::Visible);
		logRing("Far",      ChunkRing::Far);
	}
}
