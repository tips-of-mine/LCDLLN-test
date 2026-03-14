#pragma once

#include "engine/render/DecalSystem.h"
#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Push constants for one projected deferred decal.
	struct DecalParams
	{
		float invViewProj[16];
		float center[4];
		float halfExtents[4];
		float fadeAlpha = 1.0f;
		float _pad[3]{};
	};
	static_assert(sizeof(DecalParams) == 112, "DecalParams must be exactly 112 bytes");

	/// Deferred decal pass projecting albedo-only decals from a cube volume into a separate overlay target.
	class DecalPass final
	{
	public:
		/// Construct an uninitialized decal pass.
		DecalPass() = default;
		DecalPass(const DecalPass&) = delete;
		DecalPass& operator=(const DecalPass&) = delete;

		/// Create render pass, descriptor set layout, descriptor pool, sampler, and graphics pipeline.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat decalOverlayFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Render all visible decals into the decal overlay target.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idDepth, ResourceId idDecalOverlay,
			const float* invViewProjMat4,
			const std::vector<VisibleDecal>& visibleDecals,
			uint32_t frameIndex);

		/// Release Vulkan resources held by the pass.
		void Destroy(VkDevice device);

		/// Return whether the pass was initialized successfully.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass m_renderPass = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkSampler m_sampler = VK_NULL_HANDLE;
		VkSampler m_depthSampler = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};
}
