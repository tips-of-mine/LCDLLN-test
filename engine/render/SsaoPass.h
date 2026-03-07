#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// M06.2: SSAO generate pass. Reads depth + normal (view-space), kernel UBO + noise
	/// from M06.1, writes SSAO_Raw (R16F occlusion 0..1). Fullscreen pass.
	class SsaoPass
	{
	public:
		/// Push constants: invProj (position reconstruction), view (normal to view-space), proj (sample to screen). 192 bytes.
		struct SsaoParams
		{
			float invProj[16]; ///< Inverse projection matrix, column-major.
			float view[16];    ///< View matrix (world to view), for normal transform.
			float proj[16];    ///< Projection matrix (view to NDC), for projecting kernel samples.
		};
		static_assert(sizeof(SsaoParams) == 192, "SsaoParams must be 192 bytes");

		SsaoPass() = default;
		SsaoPass(const SsaoPass&) = delete;
		SsaoPass& operator=(const SsaoPass&) = delete;

		/// Creates render pass, descriptor set (depth, normal, kernel UBO, noise), pipeline.
		/// \param outputFormat  SSAO_Raw format (e.g. VK_FORMAT_R16_SFLOAT).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat outputFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records the SSAO fullscreen pass. Binds depth, normal, kernel buffer, noise view/sampler.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idDepth, ResourceId idNormal, ResourceId idSsaoRaw,
			VkBuffer kernelBuffer, VkImageView noiseView, VkSampler noiseSampler,
			const SsaoParams& params, uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout  m_setLayout           = VK_NULL_HANDLE;
		VkDescriptorPool       m_descPool            = VK_NULL_HANDLE;
		VkPipelineLayout       m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline             m_pipeline            = VK_NULL_HANDLE;
		VkSampler              m_sampler             = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t               m_maxFrames           = 2;
	};
}
