#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// M07.4: TAA fullscreen pass — reproject history via velocity, clamp 3x3 neighborhood, blend.
	/// Reads: SceneColor_LDR (current), HistoryPrev, Velocity, Depth. Writes: HistoryNext.
	class TaaPass
	{
	public:
		/// Push constants: blend alpha (~0.9). 4 bytes.
		struct TaaParams
		{
			float alpha;
			float _pad[3];
		};
		static_assert(sizeof(TaaParams) == 16, "TaaParams 16 bytes for alignment");

		TaaPass() = default;
		TaaPass(const TaaPass&) = delete;
		TaaPass& operator=(const TaaPass&) = delete;

		/// Creates render pass, descriptor set (current, history, velocity, depth), pipeline.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat outputFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Records TAA: bind current LDR, history prev, velocity, depth; render to history next.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idCurrentLDR, ResourceId idHistoryPrev, ResourceId idVelocity, ResourceId idDepth,
			ResourceId idHistoryNext,
			const TaaParams& params, uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass     = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
		VkDescriptorPool      m_descPool       = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline            m_pipeline       = VK_NULL_HANDLE;
		VkSampler             m_sampler        = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t              m_maxFrames      = 2;
	};
}
