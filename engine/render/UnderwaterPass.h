#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Fullscreen underwater post-effect pass (M37.3).
	///
	/// Applied when the camera is below the water plane (camera.y < water.y).
	/// Reads SceneColor_HDR and the scene depth buffer, then applies:
	///   1. Blue colour tint:  color *= vec3(0.5, 0.7, 1.0)
	///   2. Exponential depth-based fog: fogFactor = 1 - exp(-density * linearDepth)
	///   3. Optional slight Gaussian blur vignette (light diffusion simulation)
	///
	/// The `underwaterFactor` push-constant controls overall effect intensity:
	///   0.0 → pass-through (no effect, safe to record even above water)
	///   1.0 → full underwater effects
	///
	/// Pipeline: fullscreen triangle (3 vertices, no vertex buffer).
	/// Descriptor set 0:
	///   binding 0 = SceneColor_HDR (combined image sampler, R16G16B16A16_SFLOAT)
	///   binding 1 = depth texture   (combined image sampler, D32_SFLOAT sampled as .r)
	/// Output format = same as sceneColorHDRFormat (R16G16B16A16_SFLOAT).
	class UnderwaterPass
	{
	public:
		/// Push constants sent to the underwater fragment shader each frame (M37.3).
		/// Layout must match the `push_constant` block in underwater.frag (16 bytes).
		struct UnderwaterParams
		{
			/// Effect blend factor: 0.0 = no effect (above water), 1.0 = full underwater FX.
			/// Smooth transitions are achieved by linearly ramping this value when the camera
			/// crosses the water surface (camera.y ≶ waterLevel).
			float underwaterFactor = 0.0f;
			/// Exponential fog density (spec: 0.05). Higher values = thicker fog.
			float fogDensity       = 0.05f;
			/// Camera near clip plane distance (metres).  Used to linearise the depth buffer.
			float nearZ            = 0.1f;
			/// Camera far clip plane distance (metres).  Used to linearise the depth buffer.
			float farZ             = 1000.0f;
		};
		static_assert(sizeof(UnderwaterParams) == 16, "UnderwaterParams must be 16 bytes");

		UnderwaterPass() = default;
		UnderwaterPass(const UnderwaterPass&) = delete;
		UnderwaterPass& operator=(const UnderwaterPass&) = delete;

		/// Creates the render pass, descriptor set layout, descriptor pool, samplers,
		/// and graphics pipeline.
		///
		/// \param sceneColorHDRFormat  Output format (should be VK_FORMAT_R16G16B16A16_SFLOAT).
		/// \param vertSpirv / vertWordCount  SPIR-V for underwater.vert.
		/// \param fragSpirv / fragWordCount  SPIR-V for underwater.frag.
		/// \param maxFrames  Number of in-flight frames (one descriptor set per frame).
		/// \return true on success.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
		          VkFormat sceneColorHDRFormat,
		          const uint32_t* vertSpirv, size_t vertWordCount,
		          const uint32_t* fragSpirv, size_t fragWordCount,
		          uint32_t maxFrames = 2,
		          VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Records the underwater post-effect pass into \p cmd.
		///
		/// Reads \p idSceneColorHDR and \p idDepth (both must be in SampledRead state),
		/// applies tint + fog + optional blur, and writes the modified HDR colour to
		/// \p idUnderwaterHDR (must be in ColorWrite state).
		///
		/// When \p params.underwaterFactor is 0.0, the shader is a no-op pass-through,
		/// so this can safely be called every frame regardless of underwater state.
		///
		/// \param frameIndex  Current in-flight frame index (0 .. maxFrames-1).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		            VkExtent2D extent,
		            ResourceId idSceneColorHDR,
		            ResourceId idDepth,
		            ResourceId idUnderwaterHDR,
		            const UnderwaterParams& params,
		            uint32_t frameIndex);

		/// Releases all Vulkan resources. Safe to call even when not initialised.
		void Destroy(VkDevice device);

		/// Returns true when the pipeline and render pass are valid.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_colorSampler        = VK_NULL_HANDLE; ///< Linear clamp for HDR input.
		VkSampler             m_depthSampler        = VK_NULL_HANDLE; ///< Nearest clamp for depth.

		std::vector<VkDescriptorSet> m_descriptorSets; ///< One per in-flight frame.
		uint32_t m_maxFrames = 2;
	};

} // namespace engine::render
