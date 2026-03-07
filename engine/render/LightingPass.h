#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Deferred fullscreen lighting pass (PBR GGX, metallic/roughness, UE4-like).
	///
	/// Reads GBuffer A (albedo), B (normal), C (ORM: AO/Roughness/Metallic) and Depth,
	/// reconstructs world position via the inverse view-projection matrix,
	/// applies one directional light + IBL (split-sum diffuse + specular) or constant
	/// ambient fallback, and writes SceneColor_HDR (R16G16B16A16_SFLOAT). M05.4.
	///
	/// Pipeline: fullscreen triangle (3 vertices, no vertex buffer).
	/// Descriptor set 0: 8 combined image samplers (GBufA, GBufB, GBufC, Depth, irradiance,
	/// prefiltered specular, BRDF LUT, SSAO_Blur). Push constants (132 bytes): invVP, cameraPos,
	/// lightDir, lightColor, ambientColor, useIBL.
	class LightingPass
	{
	public:
		/// CPU-side representation of the lighting push constants sent each frame.
		/// Layout must match the GLSL push_constant block in lighting.frag exactly.
		/// All vectors are 4-floats (xyz used, w = pad/unused).
		struct LightParams
		{
			float invViewProj[16]; ///< Inverse view-projection matrix, column-major (64 bytes).
			float cameraPos[4];   ///< Camera world-space position xyz, w unused (16 bytes).
			float lightDir[4];    ///< Normalized direction *toward* the light, xyz, w unused.
			float lightColor[4];   ///< RGB radiance (color * intensity), w unused.
			float ambientColor[4]; ///< Constant ambient RGB, w unused (fallback when IBL absent).
			float useIBL;          ///< 1.0 = use IBL (irradiance + prefilter + BRDF LUT), 0.0 = fallback.
		};
		static_assert(sizeof(LightParams) == 132, "LightParams must be exactly 132 bytes");

		LightingPass() = default;
		LightingPass(const LightingPass&) = delete;
		LightingPass& operator=(const LightingPass&) = delete;

		/// Creates the render pass, descriptor set layout, descriptor pool, samplers, and pipeline.
		/// \param sceneColorHDRFormat  Output image format (should be VK_FORMAT_R16G16B16A16_SFLOAT).
		/// \param vertSpirv / vertWordCount  Vertex shader SPIR-V words.
		/// \param fragSpirv / fragWordCount  Fragment shader SPIR-V words.
		/// \param maxFrames  Number of in-flight frames; one descriptor set is allocated per frame.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records the lighting pass into cmd.
		/// Updates the frame's descriptor set with the current GBuffer and IBL image views,
		/// creates a temporary framebuffer, begins the render pass, draws the fullscreen triangle,
		/// and ends the render pass (framebuffer is destroyed immediately after).
		/// When irradianceView is VK_NULL_HANDLE, IBL is disabled (useIBL=0, constant ambient).
		/// \param irradianceView / irradianceSampler  Irradiance cubemap (M05.2); may be null.
		/// \param prefilterView / prefilterSampler     Prefiltered specular cubemap (M05.3).
		/// \param brdfLutView / brdfLutSampler         BRDF LUT 2D (M05.1).
		/// \param frameIndex  Current in-flight frame index (0 .. maxFrames-1).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idGBufA, ResourceId idGBufB, ResourceId idGBufC, ResourceId idDepth,
			ResourceId idSceneColorHDR, ResourceId idSsaoBlur,
			VkImageView irradianceView, VkSampler irradianceSampler,
			VkImageView prefilterView, VkSampler prefilterSampler,
			VkImageView brdfLutView, VkSampler brdfLutSampler,
			const LightParams& params, uint32_t frameIndex);

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
		VkSampler             m_sampler             = VK_NULL_HANDLE; ///< Nearest clamp, for GBuf color channels.
		VkSampler             m_depthSampler        = VK_NULL_HANDLE; ///< Nearest clamp, no compare, for depth.

		std::vector<VkDescriptorSet> m_descriptorSets; ///< One per in-flight frame.
		uint32_t m_maxFrames = 2;
	};

} // namespace engine::render
