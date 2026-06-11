#pragma once

#include "src/client/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

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
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Records the SSAO fullscreen pass. Binds depth, normal, kernel buffer, noise view/sampler.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idDepth, ResourceId idNormal, ResourceId idSsaoRaw,
			VkBuffer kernelBuffer, VkImageView noiseView, VkSampler noiseSampler,
			const SsaoParams& params, uint32_t frameIndex);

		void Destroy(VkDevice device);

		/// Détruit les framebuffers cachés (appeler au resize avant FG destroy).
		void InvalidateFramebufferCache(VkDevice device);

		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		// Audit 2026-06-10 (Lot B2) — cache framebuffer (pattern WaterPass) :
		// l'ancien framebuffer temporaire était détruit avant le vkQueueSubmit (UB).
		struct FramebufferKey
		{
			VkImageView outputView = VK_NULL_HANDLE;
			uint32_t width = 0;
			uint32_t height = 0;
			bool operator==(const FramebufferKey& o) const noexcept
			{
				return outputView == o.outputView && width == o.width && height == o.height;
			}
		};
		struct FramebufferKeyHash
		{
			size_t operator()(const FramebufferKey& k) const noexcept
			{
				const size_t hView = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.outputView));
				const size_t hW = std::hash<uint32_t>{}(k.width);
				const size_t hH = std::hash<uint32_t>{}(k.height);
				return hView ^ (hW + 0x9e3779b9u) ^ (hH + 0x85ebca6bu);
			}
		};
		std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> m_framebufferCache;

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
