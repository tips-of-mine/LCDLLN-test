#include "engine/render/GpuUploadQueue.h"
#include "engine/core/Config.h"

namespace engine::render
{
	namespace
	{
		constexpr size_t kDefaultBudgetMb = 32;
		constexpr size_t kBytesPerMb = 1024 * 1024;
	}

	void GpuUploadQueue::Init(const engine::core::Config& config)
	{
		const int64_t mb = config.GetInt("streaming.upload_budget_mb", static_cast<int64_t>(kDefaultBudgetMb));
		m_budgetBytes = (mb > 0) ? static_cast<size_t>(mb) * kBytesPerMb : kDefaultBudgetMb * kBytesPerMb;
		m_budgetUsedThisFrame = 0;
		m_terrainQueue.clear();
		m_textureQueue.clear();
	}

	void GpuUploadQueue::Enqueue(UploadCategory category, size_t sizeBytes)
	{
		if (sizeBytes == 0)
			return;
		UploadJob job;
		job.category = category;
		job.sizeBytes = sizeBytes;
		if (category == UploadCategory::TerrainHlod)
			m_terrainQueue.push_back(job);
		else
			m_textureQueue.push_back(job);
	}

	std::vector<UploadJob> GpuUploadQueue::PlanFrameUploads()
	{
		m_budgetUsedThisFrame = 0;
		std::vector<UploadJob> out;
		out.reserve(m_terrainQueue.size() + m_textureQueue.size());

		// First: terrain/HLOD
		while (!m_terrainQueue.empty() && m_budgetUsedThisFrame + m_terrainQueue.front().sizeBytes <= m_budgetBytes)
		{
			UploadJob job = m_terrainQueue.front();
			m_terrainQueue.pop_front();
			m_budgetUsedThisFrame += job.sizeBytes;
			out.push_back(job);
		}

		// Then: texture mips
		while (!m_textureQueue.empty() && m_budgetUsedThisFrame + m_textureQueue.front().sizeBytes <= m_budgetBytes)
		{
			UploadJob job = m_textureQueue.front();
			m_textureQueue.pop_front();
			m_budgetUsedThisFrame += job.sizeBytes;
			out.push_back(job);
		}

		return out;
	}
}
