#pragma once

#include "engine/render/PsoKey.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <string>
#include <vector>

namespace engine::render
{
	/// M18.5: Vulkan pipeline cache with disk load/save.
	/// Handles invalid cache data gracefully (e.g. different driver).
	class PipelineCache
	{
	public:
		PipelineCache() = default;
		PipelineCache(const PipelineCache&) = delete;
		PipelineCache& operator=(const PipelineCache&) = delete;

		/// Creates pipeline cache; optionally loads initial data from file.
		/// \param cacheFilePath Full path (e.g. paths.content + "/cache/pipeline_cache.bin").
		/// \return true if cache was created (with or without loaded data).
		bool Init(VkDevice device, const std::string& cacheFilePath);

		/// Gets cache data, writes to file (creating parent dir if needed), then destroys cache.
		void Destroy(VkDevice device);

		VkPipelineCache GetHandle() const { return m_cache; }
		bool IsValid() const { return m_cache != VK_NULL_HANDLE; }

		/// M18.5: Warmup phase — only during this phase is pipeline creation allowed (debug assert).
		static void BeginWarmup();
		static void EndWarmup();
		static bool IsWarmupPhase();

		/// M18.5: Register a PSO key during warmup (for warmup list / logging).
		static void RegisterWarmupKey(const PsoKey& key);
		/// Returns number of PSO keys registered during warmup.
		static size_t GetWarmupKeyCount();
		/// Clears warmup list (called at end of warmup).
		static void ClearWarmupList();

	private:
		VkPipelineCache m_cache = VK_NULL_HANDLE;
		std::string m_cacheFilePath;
	};

	/// M18.5: Debug-only assert that pipeline creation is allowed (during warmup).
	/// Call before vkCreateGraphicsPipelines / vkCreateComputePipelines.
	void AssertPipelineCreationAllowed();
}
