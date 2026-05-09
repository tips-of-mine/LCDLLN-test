#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace engine::core { class Config; }

namespace engine::render
{
	/// Upload category for planning order: terrain/HLOD first, then texture mips (M10.4).
	enum class UploadCategory : uint8_t
	{
		TerrainHlod,
		TextureMip
	};

	/// One upload job (category + size in bytes).
	struct UploadJob
	{
		UploadCategory category = UploadCategory::TerrainHlod;
		size_t sizeBytes = 0;
	};

	/// GPU upload queue with per-frame budget; plan uploads (terrain first, then textures) and report stats (M10.4).
	class GpuUploadQueue
	{
	public:
		GpuUploadQueue() = default;

		/// Initializes budget from config (streaming.upload_budget_mb, default 32).
		void Init(const engine::core::Config& config);

		/// Enqueues an upload job (category + size in bytes).
		void Enqueue(UploadCategory category, size_t sizeBytes);

		/// Plans uploads for this frame: returns jobs that fit in the budget, in order (terrain/HLOD first, then texture mip).
		/// Removes returned jobs from the pending queue. Call once per frame.
		std::vector<UploadJob> PlanFrameUploads();

		/// Returns the budget in bytes for one frame.
		size_t GetBudgetBytes() const { return m_budgetBytes; }
		/// Returns bytes consumed this frame (sum of jobs returned by last PlanFrameUploads).
		size_t GetBudgetUsedThisFrame() const { return m_budgetUsedThisFrame; }
		/// Resets per-frame stats (call at start of frame if not using PlanFrameUploads to drive it).
		void ResetFrameStats() { m_budgetUsedThisFrame = 0; }

	private:
		std::deque<UploadJob> m_terrainQueue;
		std::deque<UploadJob> m_textureQueue;
		size_t m_budgetBytes = 32 * 1024 * 1024; // 32 MB default
		size_t m_budgetUsedThisFrame = 0;
	};
}
