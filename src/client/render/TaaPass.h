#pragma once

#include "src/client/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

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
