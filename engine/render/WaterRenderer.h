#pragma once

#include "engine/render/Camera.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Parameters controlling the water plane appearance and mesh (M37.1 / M37.2).
	struct WaterParams
	{
		/// World-space Y coordinate of the flat water surface.
		float waterLevel        = 0.0f;
		/// Water grid side resolution (quad count per axis).  Total quads = gridResolution^2.
		uint32_t gridResolution = 32u;
		/// World-space extent of the full water plane (half-size per axis from the origin).
		float gridHalfSize      = 256.0f;
		/// UV tiling multiplier for the scrolling normal map (ripples).
		float normalTiling      = 8.0f;
		/// UV scroll speed on the world X axis (metres per second equivalent in UV space).
		float normalScrollX     = 0.02f;
		/// UV scroll speed on the world Z axis.
		float normalScrollZ     = 0.015f;
		/// Deep water colour RGB (fully opaque zone far from shore).
		float waterColorR       = 0.02f;
		float waterColorG       = 0.15f;
		float waterColorB       = 0.25f;
		/// Fresnel base reflectance F0 for water (≈ 0.04 for fresh water).
		float fresnelF0         = 0.04f;
		/// Depth at which the water transitions from transparent to opaque (metres).
		float fadeDepthMeters   = 4.0f;

		// ---- M37.2 additions ------------------------------------------------

		/// Shore wave peak-to-trough amplitude in metres (M37.2 spec: 0.2 m).
		float waveAmplitude     = 0.2f;
		/// Shore wave frequency in radians per metre (controls wavelength).
		float waveFrequency     = 1.0f;
		/// Depth (metres) below which the foam texture is blended in (M37.2 spec: 0.5 m).
		float foamThreshold     = 0.5f;
		/// UV tiling multiplier for the foam and caustics textures.
		float causticsTiling    = 4.0f;
		/// UV scroll speed for the animated caustics texture (applied equally to both axes).
		float causticsScroll    = 0.03f;
	};

	/// Push constants sent to the water vertex + fragment shaders each frame (M37.1 / M37.2).
	/// Layout must match the `push_constant` block in water.vert and water.frag.
	///
	/// Byte layout (128 bytes total = 32 floats):
	///   viewProj[16]  (0..63)   — view-projection matrix
	///   cameraPos[4]  (64..79)  — camera world position xyz, w unused
	///   waterLevel    (80)      — water Y
	///   time          (84)      — elapsed seconds
	///   normalTiling  (88)      — normal-map tiling
	///   normalScrollX (92)      — normal-map scroll X
	///   normalScrollZ (96)      — normal-map scroll Z
	///   fresnelF0     (100)     — Fresnel F0
	///   fadeDepthMeters(104)    — depth fade range
	///   waveAmplitude (108)     — M37.2 shore wave amplitude (metres)
	///   waveFrequency (112)     — M37.2 shore wave frequency (rad/m)
	///   foamThreshold (116)     — M37.2 depth at which foam appears (metres)
	///   causticsTiling(120)     — M37.2 caustics UV tiling
	///   causticsScroll(124)     — M37.2 caustics UV scroll speed
	struct WaterPushConstants
	{
		float viewProj[16];        ///< Current frame view-projection matrix.
		float cameraPos[4];        ///< Camera world position (xyz, w unused).
		float waterLevel;          ///< Y position of the water plane.
		float time;                ///< Elapsed time in seconds (UV scroll + wave animation).
		float normalTiling;        ///< Normal map tiling.
		float normalScrollX;       ///< Normal map scroll X.
		float normalScrollZ;       ///< Normal map scroll Z.
		float fresnelF0;           ///< Fresnel base F0.
		float fadeDepthMeters;     ///< Depth at which water becomes fully opaque.
		float waveAmplitude;       ///< M37.2 — shore wave peak amplitude in metres.
		float waveFrequency;       ///< M37.2 — shore wave frequency in rad/m.
		float foamThreshold;       ///< M37.2 — depth (m) below which foam is blended.
		float causticsTiling;      ///< M37.2 — caustics texture tiling multiplier.
		float causticsScroll;      ///< M37.2 — caustics UV scroll speed (both axes).
	};
	static_assert(sizeof(WaterPushConstants) == 128, "WaterPushConstants must be 128 bytes");

	/// Water renderer: tiled plane mesh, reflection + refraction render targets,
	/// and a forward Fresnel/normal-map water surface pass (M37.1).
	///
	/// Pipeline overview:
	///   - Reflection RT   (half-res, sceneColorHDRFormat): scene rendered from Y-mirrored camera.
	///   - Refraction RT   (full-res, sceneColorHDRFormat): scene rendered from normal camera.
	///   - Water pass      (forward, additive-blend onto sceneColorHDR): water surface drawn
	///                      reading reflection + refraction + per-frame normal map scroll.
	///
	/// Usage per frame:
	///   1. Call ComputeReflectionCamera() to get the Y-mirrored camera.
	///   2. Record the scene into the reflection RT from the reflected camera.
	///   3. (Optional) Record the scene into the refraction RT from the normal camera.
	///   4. Call Record() to draw the water surface.
	class WaterRenderer final
	{
	public:
		WaterRenderer() = default;
		WaterRenderer(const WaterRenderer&) = delete;
		WaterRenderer& operator=(const WaterRenderer&) = delete;

		/// Initialise the water renderer:
		///   - allocates the water plane vertex + index buffers,
		///   - creates the reflection RT (half-res) and refraction RT (full-res),
		///   - creates the forward render pass, descriptor layout, pipeline.
		///
		/// \param vertSpirv / vertWordCount  SPIR-V for water.vert.
		/// \param fragSpirv / fragWordCount  SPIR-V for water.frag.
		/// \param maxFrames                  In-flight frame count (one descriptor set per frame).
		bool Init(VkDevice device,
		          VkPhysicalDevice physicalDevice,
		          uint32_t sceneWidth,
		          uint32_t sceneHeight,
		          VkFormat sceneColorHDRFormat,
		          const uint32_t* vertSpirv, size_t vertWordCount,
		          const uint32_t* fragSpirv, size_t fragWordCount,
		          const WaterParams& params = {},
		          uint32_t maxFrames = 2u,
		          VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Destroy all Vulkan resources. Safe to call even when not initialised.
		void Destroy(VkDevice device);

		/// Recreate the reflection/refraction RTs when the swapchain is resized.
		/// Call after Destroy() only invalidates the RTs (mesh + pipeline remain).
		/// In practice, call Destroy() + Init() on resize.
		bool ResizeRenderTargets(VkDevice device,
		                         VkPhysicalDevice physicalDevice,
		                         uint32_t sceneWidth,
		                         uint32_t sceneHeight,
		                         VkFormat sceneColorHDRFormat);

		/// Returns a Camera mirrored around the water plane (Y-reflected).
		/// Use this as the viewpoint for rendering the reflection pass (M37.1 step 2).
		/// Clip planes in the scene render should discard geometry below waterLevel.
		static Camera ComputeReflectionCamera(const Camera& camera, float waterLevel);

		/// Records the water surface forward pass into \p cmd.
		/// The pass reads \p reflectionView and \p refractionView to blend via Fresnel,
		/// distorted by the animated normal map (\p normalMapView).
		/// M37.2 adds foam edge blending and caustics projection.
		///
		/// \param reflectionView  Image view of the reflection RT (or a fallback solid-color view).
		/// \param refractionView  Image view of the refraction RT (or a fallback solid-color view).
		/// \param depthView       Scene depth view for depth-fade + foam computation.
		/// \param normalMapView   Animated water normal map (VK_NULL_HANDLE → flat normal fallback).
		/// \param foamTexView     M37.2 — foam edge texture (VK_NULL_HANDLE → no foam).
		/// \param causticsTexView M37.2 — caustics animated texture (VK_NULL_HANDLE → no caustics).
		/// \param frameIndex      Current in-flight frame index (0 .. maxFrames-1).
		/// \param timeSeconds     Elapsed time used for UV animation and wave displacement.
		void Record(VkDevice device,
		            VkCommandBuffer cmd,
		            VkExtent2D sceneExtent,
		            VkFramebuffer sceneColorHDRFramebuffer,
		            VkImageView reflectionView,
		            VkImageView refractionView,
		            VkImageView depthView,
		            VkImageView normalMapView,
		            VkImageView foamTexView,
		            VkImageView causticsTexView,
		            const float* viewProjMat4,
		            const float* cameraPosWorld3,
		            float timeSeconds,
		            uint32_t frameIndex);

		bool IsValid() const
		{
			return m_pipeline != VK_NULL_HANDLE && m_vertexBuffer != VK_NULL_HANDLE;
		}

		// --- Reflection render target accessors ---

		/// VkImage for the reflection RT (scene rendered from the mirrored camera).
		VkImage     GetReflectionImage()   const { return m_reflectionImage; }
		VkImageView GetReflectionView()    const { return m_reflectionView; }
		VkExtent2D  GetReflectionExtent()  const { return m_reflectionExtent; }

		// --- Refraction render target accessors ---

		/// VkImage for the refraction RT (scene rendered from the normal camera, underwater clip).
		VkImage     GetRefractionImage()   const { return m_refractionImage; }
		VkImageView GetRefractionView()    const { return m_refractionView; }
		VkExtent2D  GetRefractionExtent()  const { return m_refractionExtent; }

		/// Number of water mesh indices (for external draw-call accounting).
		uint32_t GetIndexCount() const { return m_indexCount; }

	private:
		/// Allocate and fill the flat water plane vertex + index buffers.
		bool CreateWaterMesh(VkDevice device, VkPhysicalDevice physicalDevice);

		/// Allocate a single-colour image and view for the reflection or refraction RT.
		bool CreateRenderTarget(VkDevice device,
		                        VkPhysicalDevice physicalDevice,
		                        uint32_t width, uint32_t height,
		                        VkFormat format,
		                        VkImage& outImage,
		                        VkImageView& outView,
		                        VkDeviceMemory& outMemory);

		/// Create the forward colour render pass (single R16G16B16A16_SFLOAT attachment, LOAD_OP_LOAD).
		bool CreateRenderPass(VkDevice device, VkFormat sceneColorHDRFormat);

		/// Build descriptor set layout, pool and pipeline.
		bool CreatePipeline(VkDevice device, VkPhysicalDevice physicalDevice,
		                    VkFormat sceneColorHDRFormat,
		                    const uint32_t* vertSpirv, size_t vertWordCount,
		                    const uint32_t* fragSpirv, size_t fragWordCount,
		                    uint32_t maxFrames,
		                    VkPipelineCache pipelineCache);

		/// Destroy only the reflection/refraction images/views/memories.
		void DestroyRenderTargets(VkDevice device);

		/// Find a memory type satisfying \p typeFilter and \p properties.
		static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice,
		                               uint32_t typeFilter,
		                               VkMemoryPropertyFlags properties);

		// ---- Water plane mesh ----
		VkBuffer       m_vertexBuffer       = VK_NULL_HANDLE;
		VkDeviceMemory m_vertexMemory       = VK_NULL_HANDLE;
		VkBuffer       m_indexBuffer        = VK_NULL_HANDLE;
		VkDeviceMemory m_indexMemory        = VK_NULL_HANDLE;
		uint32_t       m_indexCount         = 0;

		// ---- Reflection RT (half-res) ----
		VkImage        m_reflectionImage    = VK_NULL_HANDLE;
		VkImageView    m_reflectionView     = VK_NULL_HANDLE;
		VkDeviceMemory m_reflectionMemory   = VK_NULL_HANDLE;
		VkExtent2D     m_reflectionExtent   = { 0, 0 };

		// ---- Refraction RT (full-res, optional) ----
		VkImage        m_refractionImage    = VK_NULL_HANDLE;
		VkImageView    m_refractionView     = VK_NULL_HANDLE;
		VkDeviceMemory m_refractionMemory   = VK_NULL_HANDLE;
		VkExtent2D     m_refractionExtent   = { 0, 0 };

		// ---- Water forward pipeline ----
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_linearSampler       = VK_NULL_HANDLE;
		VkSampler             m_depthSampler        = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;

		WaterParams m_params{};
		bool        m_initialized = false;
	};

} // namespace engine::render
