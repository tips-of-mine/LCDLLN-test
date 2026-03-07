#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Fullscreen tonemap pass: converts SceneColor_HDR (R16G16B16A16_SFLOAT)
	/// to SceneColor_LDR (R8G8B8A8_UNORM) using an ACES-ish filmic curve
	/// followed by gamma 2.2 correction.
	///
	/// Pipeline: fullscreen triangle (3 vertices, no vertex buffer).
	/// Descriptor set 0: binding 0 = SceneColor_HDR, binding 1 = LUT (optional, M08.4).
	/// Push constants: exposure (float), strength (float) for LUT blend.
	///
	/// Added in M03.4; LUT support in M08.4.
	class TonemapPass
	{
	public:
		/// Push constants sent to the tonemap fragment shader each frame.
		struct TonemapParams
		{
			float exposure; ///< Linear exposure multiplier applied before the curve.
			float strength; ///< LUT blend strength 0..1 (0 = no LUT). M08.4.
		};
		static_assert(sizeof(TonemapParams) == 8, "TonemapParams must be exactly 8 bytes");

		TonemapPass() = default;
		TonemapPass(const TonemapPass&) = delete;
		TonemapPass& operator=(const TonemapPass&) = delete;

		/// Creates the render pass, descriptor set layout, descriptor pool, sampler, and pipeline.
		/// \param sceneColorLDRFormat  Output image format (should be VK_FORMAT_R8G8B8A8_UNORM).
		/// \param vertSpirv / vertWordCount  Vertex shader SPIR-V words.
		/// \param fragSpirv / fragWordCount  Fragment shader SPIR-V words.
		/// \param maxFrames  Number of in-flight frames; one descriptor set is allocated per frame.
		/// \return true on success.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorLDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records the tonemap pass into cmd.
		/// Updates the frame descriptor set with the current HDR image view,
		/// creates a temporary framebuffer, begins the render pass, draws the
		/// fullscreen triangle, and ends the render pass (framebuffer is
		/// destroyed immediately after).
		/// \param frameIndex  Current in-flight frame index (0 .. maxFrames-1).
		/// \param lutView     Optional LUT texture view (256x16 strip or 32^3). If VK_NULL_HANDLE, LUT is disabled (binding 1 uses HDR view as fallback).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent,
			ResourceId idSceneColorHDR,
			ResourceId idSceneColorLDR,
			const TonemapParams& params,
			VkImageView lutView,
			uint32_t frameIndex);

		/// Releases all Vulkan resources. Safe to call even when not initialized.
		void Destroy(VkDevice device);

		/// Returns true if the pipeline and render pass are valid.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE; ///< Linear clamp sampler for HDR input.

		std::vector<VkDescriptorSet> m_descriptorSets; ///< One per in-flight frame.
		uint32_t m_maxFrames = 2;
	};

} // namespace engine::render
