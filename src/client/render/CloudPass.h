#pragma once
// Passe GRAPHIQUE plein écran de nuages volumétriques ray-marchés (fragment shader).
// Calquée EXACTEMENT sur VolumetricFogPass : render pass 1 attachment color,
// descriptor set 0 de 2 combined image samplers (scene color, depth), push
// constants fragment, pipeline fullscreen triangle, framebuffer mis en cache dans Record.
//
// Descriptor set 0 :
//   binding 0 = scene color HDR (post-fog)  (sampler linéaire clamp)
//   binding 1 = depth scene (D32_SFLOAT)     (sampler nearest clamp)

#include "src/client/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	class CloudPass
	{
	public:
		/// Push constants (stage fragment). Layout EXACT du bloc push_constant de
		/// clouds.frag (vec4 = 16 o, mat4 = 64 o).
		struct CloudPushConstants
		{
			float invViewProj[16];  ///< 64 o.
			float cameraPos[4];     ///< xyz caméra ; w = temps (s).        16 o.
			float sunDir[4];        ///< xyz dir vers soleil ; w = coverage. 16 o.
			float sunColor[4];      ///< xyz couleur soleil ; w = density.   16 o.
			float zenithColor[4];   ///< xyz zénith ; w = baseAltMeters.      16 o.
			float horizonColor[4];  ///< xyz horizon ; w = topAltMeters.      16 o.
			float windParams[4];    ///< x ventX ; y ventZ ; z vitesse ; w = HG g. 16 o.
			float stepParams[4];    ///< x stepsVue ; y stepsLum ; z distMax ; w = ambiant. 16 o.
			float shadowParams[4];  ///< x = force ombres sol [0..1] ; y = luminance max ; z = distance d'estompage (m) ; w = tuile weather map (m, <=1 désactive — chantier weather map 2026-07-20). 16 o.
		};
		static_assert(sizeof(CloudPushConstants) == 192, "CloudPushConstants doit faire 192 octets");

		CloudPass() = default;
		CloudPass(const CloudPass&) = delete;
		CloudPass& operator=(const CloudPass&) = delete;

		/// Crée render pass, descriptor set layout/pool, samplers, pipeline plein écran
		/// ET les deux textures 3D de bruit Perlin-Worley (chantier ciel 2026-07-17) :
		/// génération CPU (CloudNoiseGenerator, ~centaines de ms, une fois au boot)
		/// puis upload via staging + command pool transitoire sur \p uploadQueue.
		/// \param sceneColorHDRFormat doit être VK_FORMAT_R16G16B16A16_SFLOAT.
		/// \param vertSpirv/vertWordCount  SPIR-V du fullscreen triangle (lighting.vert.spv).
		/// \param fragSpirv/fragWordCount  SPIR-V de clouds.frag.spv.
		/// \param uploadQueue        Queue graphics pour l'upload one-shot des textures
		///        de bruit (vkQueueWaitIdle interne — boot uniquement, jamais en frame).
		/// \param uploadQueueFamilyIndex Famille de \p uploadQueue (command pool).
		/// \param compositeFragSpirv/compositeFragWordCount SPIR-V de
		///        clouds_composite.frag (lot 1 : upsample + composition pleine
		///        résolution des nuages marchés en cible réduite).
		/// \param maxFrames frames en vol (un descriptor set par frame).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			const uint32_t* compositeFragSpirv, size_t compositeFragWordCount,
			VkQueue uploadQueue, uint32_t uploadQueueFamilyIndex,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Lot 1 (2026-07-18) — Enregistre la MARCHE des nuages dans la cible
		/// réduite \p idCloudsOut (RGBA16F, extent \p scaledExtent = swapchain
		/// divisée par render.clouds.resolution_divider). Lit uniquement le
		/// depth (pleine résolution, échantillonné) ; sortie pré-multipliée
		/// (rgb) + visibilité scène (a), cf. clouds.frag.
		void RecordMarch(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D scaledExtent, ResourceId idDepth,
			ResourceId idCloudsOut, const CloudPushConstants& params, uint32_t frameIndex);

		/// Lot 1 (2026-07-18) — Compose (pleine résolution) la cible nuages
		/// réduite \p idCloudsIn sur la scène \p idSceneColorIn et écrit
		/// \p idSceneColorOut : final = scene * clouds.a + clouds.rgb.
		void RecordComposite(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent, ResourceId idSceneColorIn, ResourceId idCloudsIn,
			ResourceId idSceneColorOut, uint32_t frameIndex);

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

		/// Chantier ciel 2026-07-17 — Crée une image 3D R8G8B8A8_UNORM de côté
		/// \p size, y uploade \p rgba (size³×4 octets) via staging + command
		/// buffer one-shot (submit + wait sur \p queue), et crée la vue 3D.
		/// \return false (et log) au premier échec Vulkan ; les handles déjà
		/// créés sont libérés par Destroy.
		bool CreateNoiseTexture3D(VkDevice device, VkPhysicalDevice physicalDevice,
			VkCommandPool cmdPool, VkQueue queue,
			int size, const uint8_t* rgba,
			VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView);

		/// Chantier weather map 2026-07-20 — Crée une image 2D R8_UNORM de côté
		/// \p size, y uploade \p r8 (size²×1 octet) via staging + command buffer
		/// one-shot (submit + wait sur \p queue — boot uniquement), et crée la
		/// vue 2D. Stocke le résultat dans m_weatherImage/Memory/View.
		/// \return false (et log) au premier échec Vulkan.
		bool CreateWeatherTexture2D(VkDevice device, VkPhysicalDevice physicalDevice,
			VkCommandPool cmdPool, VkQueue queue,
			int size, const uint8_t* r8);

		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		// Lot 1 (2026-07-18) — pipeline de composition pleine résolution
		// (clouds_composite.frag : scene * clouds.a + clouds.rgb). Réutilise
		// m_renderPass (même format RGBA16F que la cible réduite).
		VkDescriptorSetLayout m_compositeSetLayout      = VK_NULL_HANDLE;
		VkPipelineLayout      m_compositePipelineLayout = VK_NULL_HANDLE;
		VkPipeline            m_compositePipeline       = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_compositeSets;
		VkSampler             m_linearSampler       = VK_NULL_HANDLE;
		VkSampler             m_nearestSampler      = VK_NULL_HANDLE;
		// Chantier ciel 2026-07-17 — textures 3D de bruit Perlin-Worley
		// (base 64³ + détail 32³) et leur sampler linéaire REPEAT (tuilage).
		VkImage        m_noiseBaseImage    = VK_NULL_HANDLE;
		VkDeviceMemory m_noiseBaseMemory   = VK_NULL_HANDLE;
		VkImageView    m_noiseBaseView     = VK_NULL_HANDLE;
		VkImage        m_noiseDetailImage  = VK_NULL_HANDLE;
		VkDeviceMemory m_noiseDetailMemory = VK_NULL_HANDLE;
		VkImageView    m_noiseDetailView   = VK_NULL_HANDLE;
		// Chantier weather map 2026-07-20 — weather map 2D R8 de couverture
		// (variation spatiale des nuages, binding 4, sampler REPEAT partagé).
		VkImage        m_weatherImage      = VK_NULL_HANDLE;
		VkDeviceMemory m_weatherMemory     = VK_NULL_HANDLE;
		VkImageView    m_weatherView       = VK_NULL_HANDLE;
		VkSampler      m_noiseSampler      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};
}
