#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Water surface rendering pass: tiled plane mesh, reflection/refraction RTs,
	/// Fresnel blend, animated normal ripples (M37.1), plus foam, shore waves and
	/// caustics (M37.2).
	///
	/// The pass owns two half-resolution render targets:
	///   - ReflectionRT : HDR color; populate by re-rendering the scene with the
	///     reflected camera (via BuildReflectionViewProj) before calling Record().
	///   - RefractionRT : HDR color; populate by re-rendering the underwater scene
	///     before calling Record() (optional: leave un-cleared for a simple
	///     deep-water fallback).
	///
	/// Pipeline: vertex-indexed water plane grid.
	/// Descriptor set 0: binding 0 = reflectionTex (sampler2D),
	///                   binding 1 = refractionTex (sampler2D),
	///                   binding 2 = sceneDepthTex (sampler2D) — M37.2 foam.
	/// Push constants (120 bytes): view-projection, camera pos, waterY, time, fresnelF0,
	///   planeHalfSize, waveAmplitude, foamThreshold, causticsScale, causticsIntensity,
	///   nearZ, farZ.  Layout must match water.vert / water.frag exactly.
	class WaterPass final
	{
	public:
		/// Push-constant block sent each frame.
		/// Column-major, matches the GLSL push_constant block in water.vert/.frag.
		struct WaterParams
		{
			float viewProj[16];       ///< View-projection matrix, column-major (64 bytes).
			float cameraPos[4];       ///< Camera world-space position xyz, w unused (16 bytes).
			float waterY;             ///< World-space Y coordinate of the water plane.
			float time;               ///< Elapsed seconds — drives UV scroll animation.
			float fresnelF0;          ///< Fresnel reflectance at 0° incidence (≈0.02 for water).
			float planeHalfSize;      ///< Half-extent in world units; plane spans ±planeHalfSize.
			// M37.2 — Shore waves, foam, caustics:
			float waveAmplitude;      ///< Shore wave Y displacement amplitude in metres (e.g. 0.2).
			float foamThreshold;      ///< Depth < threshold (metres) triggers foam blend (e.g. 0.5).
			float causticsScale;      ///< UV tiling scale for the caustics pattern.
			float causticsIntensity;  ///< Caustics brightness multiplier (0 = disabled).
			float nearZ;              ///< Camera near plane in world units (for depth linearisation).
			float farZ;               ///< Camera far plane in world units (for depth linearisation).
		};
		static_assert(sizeof(WaterParams) == 120, "WaterParams must be exactly 120 bytes");

		WaterPass()                            = default;
		WaterPass(const WaterPass&)            = delete;
		WaterPass& operator=(const WaterPass&) = delete;

		/// Create water plane mesh, reflection/refraction render targets, and the
		/// composite pipeline.
		///
		/// \param sceneColorHDRFormat  Output format (VK_FORMAT_R16G16B16A16_SFLOAT).
		/// \param vertSpirv/vertWordCount  Water vertex shader SPIR-V.
		/// \param fragSpirv/fragWordCount  Water fragment shader SPIR-V.
		/// \param rtWidth/rtHeight         Reflection + refraction RT dimensions
		///   (half-res recommended, e.g. swapchain/2).
		/// \param maxFrames                In-flight frame count; one descriptor set per frame.
		/// \return true on success; false leaves the pass in a safe disabled state.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t rtWidth, uint32_t rtHeight,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Composite the water surface into the SceneColor_HDR target.
		/// Call AFTER populating the reflection/refraction RTs for this frame.
		///
		/// The pass reads m_reflectionView / m_refractionView owned by this instance.
		/// \param idSceneColorHDR  FG resource that receives the composited water.
		/// \param idDepth          FG resource for scene depth (used for foam — M37.2).
		///                         Pass kInvalidResourceId to disable depth-based foam.
		/// \param frameIndex       Current in-flight frame index (0 .. maxFrames-1).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent,
			ResourceId idSceneColorHDR,
			ResourceId idDepth,
			const WaterParams& params,
			uint32_t frameIndex);

		/// Release all Vulkan resources. Safe to call when not initialised.
		void Destroy(VkDevice device);

		/// True when the pipeline is ready.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

		/// VkImageView of the reflection render target.
		/// Render the scene with the mirrored camera into this RT before Record().
		VkImageView GetReflectionView() const { return m_reflectionView; }

		/// VkImageView of the refraction render target.
		/// Render the underwater scene into this RT before Record() (optional).
		VkImageView GetRefractionView() const { return m_refractionView; }

		/// Compute the reflection view-projection matrix for the pre-pass.
		/// Mirrors the camera about the horizontal water plane at \p waterY by
		/// pre-multiplying the view-projection with a plane-reflection matrix.
		///
		/// \param viewProj     Current VP matrix (column-major, 16 floats).
		/// \param waterY       World Y of the water surface.
		/// \param outReflectVP Output reflected VP (column-major, 16 floats).
		static void BuildReflectionViewProj(
			const float* viewProj,
			float        waterY,
			float*       outReflectVP);

	private:
		// ── Water plane mesh (HOST_VISIBLE | HOST_COHERENT; small geometry) ──
		VkBuffer       m_vertexBuffer  = VK_NULL_HANDLE;
		VkDeviceMemory m_vertexMemory  = VK_NULL_HANDLE;
		VkBuffer       m_indexBuffer   = VK_NULL_HANDLE;
		VkDeviceMemory m_indexMemory   = VK_NULL_HANDLE;
		uint32_t       m_indexCount    = 0;

		// ── Reflection render target (DEVICE_LOCAL, owned by this pass) ──
		VkImage        m_reflectionImage  = VK_NULL_HANDLE;
		VkDeviceMemory m_reflectionMemory = VK_NULL_HANDLE;
		VkImageView    m_reflectionView   = VK_NULL_HANDLE;

		// ── Refraction render target (DEVICE_LOCAL, owned by this pass) ──
		VkImage        m_refractionImage  = VK_NULL_HANDLE;
		VkDeviceMemory m_refractionMemory = VK_NULL_HANDLE;
		VkImageView    m_refractionView   = VK_NULL_HANDLE;

		// ── Pipeline resources ──
		VkFormat              m_hdrFormat          = VK_FORMAT_UNDEFINED;
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE; ///< Linear clamp (reflection/refraction).
		VkSampler             m_depthSampler        = VK_NULL_HANDLE; ///< Nearest clamp for depth (M37.2 foam).

		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t                     m_maxFrames = 2;
	};

} // namespace engine::render
