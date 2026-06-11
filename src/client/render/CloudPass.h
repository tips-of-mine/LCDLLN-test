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
			float shadowParams[4];  ///< x = force ombres sol [0..1] ; z = distance d'estompage (m) ; y,w réservés. 16 o.
		};
		static_assert(sizeof(CloudPushConstants) == 192, "CloudPushConstants doit faire 192 octets");

		CloudPass() = default;
		CloudPass(const CloudPass&) = delete;
		CloudPass& operator=(const CloudPass&) = delete;

		/// Crée render pass, descriptor set layout/pool, samplers, pipeline plein écran.
		/// \param sceneColorHDRFormat doit être VK_FORMAT_R16G16B16A16_SFLOAT.
		/// \param vertSpirv/vertWordCount  SPIR-V du fullscreen triangle (lighting.vert.spv).
		/// \param fragSpirv/fragWordCount  SPIR-V de clouds.frag.spv.
		/// \param maxFrames frames en vol (un descriptor set par frame).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Enregistre la passe. Lit scene color (idSceneColorIn) + depth (idDepth),
		/// composite les nuages, écrit dans idSceneColorOut.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idSceneColorIn, ResourceId idDepth,
			ResourceId idSceneColorOut, const CloudPushConstants& params, uint32_t frameIndex);

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

		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_linearSampler       = VK_NULL_HANDLE;
		VkSampler             m_nearestSampler      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};
}
