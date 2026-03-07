#pragma once

#include "engine/world/WorldModel.h"

namespace engine::core { class Config; }

namespace engine::world
{
	/// Per-ring draw and triangle counts (M09.2).
	struct RingStats
	{
		uint32_t drawcalls = 0;
		uint32_t triangles = 0;
	};

	/// Budget and instrumentation for drawcalls/triangles per chunk/ring (M09.2).
	/// Budgets are read from config at Init (cvars); stats are accumulated per frame and reset each BeginFrame.
	class ChunkBudgetStats
	{
	public:
		ChunkBudgetStats() = default;

		/// Reads budget cvars from config and initialises per-ring counters.
		/// Config keys: world.budget_active_drawcalls (default 1000), world.budget_visible_drawcalls (300), world.budget_far_drawcalls (50).
		void Init(const engine::core::Config& config);

		/// Records one or more draws for the given chunk and ring (call from renderer for each tagged draw).
		void RecordDraw(ChunkCoord chunkId, ChunkRing ring, uint32_t drawcallCount, uint32_t triangleCount);

		/// Resets per-ring stats for the next frame. Call at start of frame (e.g. BeginFrame).
		void ResetPerFrame();

		/// Returns current stats for the given ring.
		RingStats GetRingStats(ChunkRing ring) const;

		/// Returns the configured drawcall budget for the given ring.
		uint32_t GetBudgetDrawcalls(ChunkRing ring) const;

		/// Logs per-ring stats and budget comparison to the debug log. Call periodically (e.g. every N frames).
		void LogStats() const;

	private:
		static constexpr size_t kRingCount = 3;
		RingStats m_ringStats[kRingCount]{};
		uint32_t m_budgetActiveDrawcalls  = 1000;
		uint32_t m_budgetVisibleDrawcalls  = 300;
		uint32_t m_budgetFarDrawcalls      = 50;
	};
}
