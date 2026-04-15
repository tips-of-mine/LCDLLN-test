#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Fullscreen underwater post-effects pass (M37.3).
	///
	/// Applies when the camera is below the water surface (camera.y < waterY).
	/// The caller decides whether to invoke Record(); when not underwater, simply
	/// skip the call.
	///
	/// Effect pipeline (composited onto SceneColor_HDR via alpha blending):
	///   1. Reads scene depth to compute fragment view-space distance.
	///   2. Applies exponential depth fog:  fogFactor = exp(-density * linearDepth).
	///   3. Blends a blue tint overlay over existing scene:
	///        finalColor = lerp(existingColor, fogColor, fogBlend)
	///   4. Optional vignette darkening at screen edges (simulates light scatter).
	///
	/// Pipeline: fullscreen triangle (3 vertices, no vertex buffer).
	/// Descriptor set 0: binding 0 = sceneDepth (sampler2D, NEAREST).
	/// Push constants (32 bytes): fogColor (rgb), fogDensity, nearZ, farZ,
	///   vignetteStrength, pad.
	class UnderwaterPass final
	{
	public:
		/// Push-constant block sent each frame. Layout must match underwater.frag.
		struct UnderwaterParams
		{
			float fogColorR;        ///< Fog/tint colour R (default 0.02).
			float fogColorG;        ///< Fog/tint colour G (default 0.08).
			float fogColorB;        ///< Fog/tint colour B (default 0.25).
			float fogDensity;       ///< Exponential fog density k: factor = exp(-k * depth), e.g. 0.05.
			float nearZ;            ///< Camera near plane (world units) — for depth linearisation.
			float farZ;             ///< Camera far plane (world units)  — for depth linearisation.
			float vignetteStrength; ///< Vignette edge darkening [0, 1] (0 = disabled).
			float _pad;             ///< Alignment padding.
		};
		static_assert(sizeof(UnderwaterParams) == 32, "UnderwaterParams must be exactly 32 bytes");

		UnderwaterPass()                               = default;
		UnderwaterPass(const UnderwaterPass&)          = delete;
		UnderwaterPass& operator=(const UnderwaterPass&) = delete;

		/// Create the render pass, descriptor layout, pool, sampler, and pipeline.
		///
		/// \param sceneColorHDRFormat  Format of SceneColor_HDR (color attachment output).
		/// \param vertSpirv/vertWordCount  Fullscreen triangle vertex shader SPIR-V.
		/// \param fragSpirv/fragWordCount  Underwater effect fragment shader SPIR-V.
		/// \param maxFrames                In-flight frame count; one descriptor set per frame.
		/// \return true on success; false leaves the pass disabled.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Composite the underwater effect onto SceneColor_HDR.
		/// Call only when the camera is below the water surface.
		///
		/// \param idSceneColorHDR  FG resource receiving the blended underwater overlay.
		/// \param idDepth          FG resource for the scene depth buffer.
		/// \param params           Per-frame fog/tint parameters.
		/// \param frameIndex       Current in-flight frame index (0 .. maxFrames-1).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent,
			ResourceId idSceneColorHDR,
			ResourceId idDepth,
			const UnderwaterParams& params,
			uint32_t frameIndex);

		/// Release all Vulkan resources. Safe to call when not initialised.
		void Destroy(VkDevice device);

		/// True when the pipeline is ready.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

		/// Detect whether the camera is currently underwater.
		/// \param cameraY  World-space Y of the camera.
		/// \param waterY   World-space Y of the water surface.
		/// \return true when cameraY < waterY.
		static bool IsUnderwater(float cameraY, float waterY) { return cameraY < waterY; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE; ///< NEAREST clamp for depth.

		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t                     m_maxFrames = 2;
	};

} // namespace engine::render
