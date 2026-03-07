#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// M06.3: Bilateral blur for SSAO (2 passes: H then V). Depth-aware weights to avoid
	/// bleeding over depth discontinuities. Fullscreen pass, 5–7 taps.
	class SsaoBlurPass
	{
	public:
		/// Push constants: texel size and blur direction. 16 bytes.
		struct BlurParams
		{
			float texelSizeX;  ///< 1.0 / width.
			float texelSizeY;  ///< 1.0 / height.
			float horizontal; ///< 1.0 = horizontal pass, 0.0 = vertical pass.
			float _pad;
		};
		static_assert(sizeof(BlurParams) == 16, "BlurParams must be 16 bytes");

		SsaoBlurPass() = default;
		SsaoBlurPass(const SsaoBlurPass&) = delete;
		SsaoBlurPass& operator=(const SsaoBlurPass&) = delete;

		/// Creates render pass, descriptor set (input AO, depth), pipeline.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat outputFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records one blur pass (horizontal or vertical). Binds input AO and depth.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idInputAo, ResourceId idDepth, ResourceId idOutput,
			bool horizontal, uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass     = VK_NULL_HANDLE;
		VkDescriptorSetLayout  m_setLayout      = VK_NULL_HANDLE;
		VkDescriptorPool      m_descPool       = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline            m_pipeline       = VK_NULL_HANDLE;
		VkSampler             m_sampler        = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t              m_maxFrames      = 2;
	};
}
