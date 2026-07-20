#pragma once

#include "src/client/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
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
		/// Descriptor set 0: 10 combined image samplers (GBufA, GBufB, GBufC, Depth, irradiance,
		/// prefiltered specular, BRDF LUT, SSAO_Blur, DecalOverlay, DDGI irradiance [M45.7]).
	/// Push constants (224 bytes): invVP, cameraPos, lightDir, lightColor, ambientColor, skyColor,
	/// useIBL, puis champs DDGI (useDdgi + grille/atlas) ajoutés en M45.7.
	class LightingPass
	{
	public:
		/// CPU-side representation of the lighting push constants sent each frame.
		/// Layout must match the GLSL push_constant block in lighting.frag exactly.
		/// All vectors are 4-floats (xyz used, w = pad/unused).
		struct LightParams
		{
			float invViewProj[16]; ///< Inverse view-projection matrix, column-major (64 bytes).
			float cameraPos[4];   ///< Camera world-space position xyz ; w = M45.1 aerial fogStart (m).
			float lightDir[4];    ///< Normalized direction *toward* the light xyz ; w = M45.1 aerial fogEnd (m).
			float lightColor[4];   ///< RGB radiance (color * intensity) ; w = M45.1 aerialDensity (1/m, <=0 désactive).
			float ambientColor[4]; ///< Constant ambient RGB (fallback when IBL absent) ; w = M45.1 aerialInscatter.
			float skyColor[4];     ///< RGB couleur du ciel (skyHorizon DayNightCycle) pour les pixels sans géométrie. w = 1.0 si SkyPass a déjà écrit le ciel dans GBufferA (cf. lighting.frag).
			float useIBL;          ///< 1.0 = use IBL (irradiance + prefilter + BRDF LUT), 0.0 = fallback.
			// --- M45.7 — DDGI dynamique (ADDITIF). useDdgi=0 (défaut) => chemin
			// strictement inchangé (binding 9 lié à un fallback valide mais jamais lu).
			// useIBL + useDdgi + pad0 + pad1 = 16 octets pour que les vec4 DDGI
			// suivants soient alignés sur 16 (règle std140 des push_constant GLSL).
			float useDdgi;         ///< 1.0 = échantillonne l'irradiance DDGI dynamique, 0.0 = aucun changement.
			float aerialSkyModel = 0.0f; ///< Chantier perspective aérienne 2026-07-20 : 1.0 = la teinte aerial est le ciel ANALYTIQUE évalué par rayon (cohérent avec sky.frag) ; 0.0 = skyColor legacy (ex-pad0, layout inchangé).
			float pad1;            ///< Padding (alignement vec4 std140).
			float ddgiOrigin[4];   ///< xyz = origine monde de la grille DDGI (mètres) ; w inutilisé.
			float ddgiSpacing[4];  ///< xyz = espacement par axe (mètres) ; w inutilisé.
			float ddgiCounts[4];   ///< xyz = nb de sondes par axe ; w = irradianceTexels (côté hors bordure).
			float ddgiAtlas[4];    ///< x = atlasCols, y = atlasRows, z = tileSize (texels+2), w = intensity.
		};
		// 144 (jusqu'à skyColor) + 16 (useIBL+useDdgi+pad0+pad1) + 64 (4 vec4 DDGI) = 224.
		static_assert(sizeof(LightParams) == 224, "LightParams must be exactly 224 bytes (144 + 16 + 64 DDGI)");

		/// CPU-side de l'UBO cascades (binding 11, std140). 4 lightViewProj (256 o)
		/// + shadowParams (16 o) = 272 o. SÉPARÉ de LightParams (push-constant figé
		/// à 224 o). shadowParams : x=useShadows(0/1), y=texelSize(1/résolution),
		/// z=biasConstant, w=biasSlopeMax. Voir lighting.frag binding 11.
		struct ShadowUbo
		{
			float lightViewProj[16 * 4]; ///< 4 matrices colonne-major (cascade 0..3), 256 o.
			float shadowParams[4];       ///< x=useShadows, y=texelSize, z=biasConstant, w=biasSlopeMax.
		};
		static_assert(sizeof(ShadowUbo) == 272, "ShadowUbo must be exactly 272 bytes (256 + 16)");

		/// CPU-side de l'UBO point lights (binding 12, std140). SÉPARÉ de LightParams
		/// (push-constant 224 o) et de ShadowUbo. count.x = nb lumières [0..64] ;
		/// lights[i].posRadius (xyz=position monde m, w=rayon m) ; colorIntensity
		/// (rgb=couleur linéaire, a=intensité×intensity_scale). Voir lighting.frag b12.
		struct PointLightStd140
		{
			float posRadius[4];      ///< xyz = position monde (m) ; w = rayon (m).
			float colorIntensity[4]; ///< rgb = couleur linéaire ; a = intensité finale.
		};
		struct PointLightUbo
		{
			uint32_t        count[4];    ///< x = nb actives [0..64] ; yzw padding std140.
			PointLightStd140 lights[64]; ///< std140, stride 32 o/élément.
		};
		static_assert(sizeof(PointLightUbo) == 2064, "PointLightUbo must be 2064 bytes (16 + 64*32)");

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
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Records the lighting pass into cmd.
		/// Updates the frame's descriptor set with the current GBuffer and IBL image views,
		/// fetches (or creates) the framebuffer from the internal cache, begins the render
		/// pass, draws the fullscreen triangle, and ends the render pass.
		/// When irradianceView is VK_NULL_HANDLE, IBL is disabled (useIBL=0, constant ambient).
		/// \param idDecalOverlay  M17.3 decal overlay texture written after geometry and composited into albedo.
		/// \param irradianceView / irradianceSampler  Irradiance cubemap (M05.2); may be null.
		/// \param prefilterView / prefilterSampler     Prefiltered specular cubemap (M05.3).
		/// \param brdfLutView / brdfLutSampler         BRDF LUT 2D (M05.1).
		/// \param ddgiIrradianceView / ddgiSampler     M45.7 — atlas d'irradiance DDGI dynamique
		///        (binding 9). Quand ddgiIrradianceView == VK_NULL_HANDLE (défaut),
		///        un fallback valide (GBufferA / m_sampler) est lié pour garder le
		///        descriptor valide ; params.useDdgi=0 garantit que le shader ne le lit pas.
		/// \param frameIndex  Current in-flight frame index (0 .. maxFrames-1).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idGBufA, ResourceId idGBufB, ResourceId idGBufC, ResourceId idDepth,
			ResourceId idSceneColorHDR, ResourceId idSsaoBlur, ResourceId idDecalOverlay,
			VkImageView irradianceView, VkSampler irradianceSampler,
			VkImageView prefilterView, VkSampler prefilterSampler,
			VkImageView brdfLutView, VkSampler brdfLutSampler,
			VkImageView ddgiIrradianceView, VkSampler ddgiSampler,
			/// \param shadowViews 4 vues des shadow maps cascades (binding 10). Toute
			///        entrée VK_NULL_HANDLE => fallback GBufferA ; shadowData.shadowParams[0]
			///        (useShadows) doit alors valoir 0 (le shader ne lit pas le fallback).
			/// \param shadowData lightViewProj[4] (256 o) + shadowParams (16 o), uploadés
			///        dans l'UBO host-visible binding 11.
			const VkImageView shadowViews[4], const ShadowUbo& shadowData,
			/// \param pointLightData count + lights[64] (2064 o), uploadés dans l'UBO
			///        binding 12. count.x==0 => boucle point lights sautée (rendu inchangé).
			const PointLightUbo& pointLightData,
			const LightParams& params, uint32_t frameIndex);

		/// Releases all Vulkan resources. Safe to call even when not initialized.
		void Destroy(VkDevice device);

		/// Détruit les framebuffers cachés (appeler au resize avant FG destroy).
		void InvalidateFramebufferCache(VkDevice device);

		/// Returns true if the pipeline and render pass are valid.
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
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE; ///< Nearest clamp, for GBuf color channels.
		VkSampler             m_depthSampler        = VK_NULL_HANDLE; ///< Nearest clamp, no compare, for depth.
		VkSampler m_shadowSampler = VK_NULL_HANDLE; ///< nearest clamp, lecture plain de la profondeur shadow (binding 10).
		std::vector<VkBuffer>       m_shadowUboBuffers; ///< UBO cascades host-visible, un par frame (binding 11).
		std::vector<VkDeviceMemory> m_shadowUboMemory;  ///< Mémoire host-visible.
		std::vector<void*>          m_shadowUboMapped;  ///< Pointeurs mappés persistants (HOST_VISIBLE|HOST_COHERENT).
		std::vector<VkBuffer>       m_pointLightUboBuffers; ///< UBO point lights host-visible, un par frame (binding 12).
		std::vector<VkDeviceMemory> m_pointLightUboMemory;  ///< Mémoire host-visible.
		std::vector<void*>          m_pointLightUboMapped;  ///< Pointeurs mappés persistants.

		std::vector<VkDescriptorSet> m_descriptorSets; ///< One per in-flight frame.
		uint32_t m_maxFrames = 2;
	};

} // namespace engine::render
