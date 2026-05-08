#pragma once

#include "engine/render/FrameGraph.h"
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterSurfaces.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Push constants par-instance pour la passe water (128 B exacts).
	/// Layout aligné std140 (les vec3 prennent 16 B avec padding).
	/// Offsets vérifiés par tests offsetof — toute modification doit aussi
	/// mettre à jour le layout GLSL `push_constant` correspondant dans
	/// engine/render/shaders/water.vert + water.frag.
	struct WaterPassPushConstants
	{
		float viewProj[16];        // offset   0, size 64
		float cameraPos[3];        // offset  64, size 12
		float timeSeconds;         // offset  76, size  4
		float bottomColor[3];      // offset  80, size 12
		float turbidity;           // offset  92, size  4
		float flowDirection[2];    // offset  96, size  8
		float flowSpeed;           // offset 104, size  4
		float refractionAmount;    // offset 108, size  4
		float fresnelPower;        // offset 112, size  4
		float reflectionStrength;  // offset 116, size  4
		float screenSize[2];       // offset 120, size  8
	};
	static_assert(sizeof(WaterPassPushConstants) == 128, "WaterPassPushConstants must be exactly 128 bytes");

	class WaterPass final
	{
	public:
		WaterPass() = default;
		WaterPass(const WaterPass&) = delete;
		WaterPass& operator=(const WaterPass&) = delete;

		/// Crée render pass + descriptor layout/pool + samplers + pipeline.
		/// \param normalMapView    Tile normale water (8-bit) ; VK_NULL_HANDLE → init échoue.
		/// \param skyboxCubeView   Skybox cube pour fallback réflexion ; VK_NULL_HANDLE → init échoue.
		/// \warning normalMapView, normalMapSampler, skyboxCubeView et skyboxSampler
		///          sont référencés jusqu'à Destroy() — le caller doit les maintenir en vie.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			VkImageView normalMapView, VkSampler normalMapSampler,
			VkImageView skyboxCubeView, VkSampler skyboxSampler,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Enregistre la passe : begin render pass + bind pipeline + bind vertex/index +
		/// loop drawInfos avec push constants par-instance + draw indexed.
		///
		/// \param idSceneColorIn   Resource ID SceneColor_HDR (déclaré SampledRead par le caller).
		/// \param idSceneDepth     Resource ID SceneDepth (déclaré SampledRead par le caller).
		/// \param idSceneColorOut  Resource ID SceneColor_HDR_PostWater (déclaré ColorWrite par le caller).
		/// \param mesh             Buffer GPU contenant VBO/IBO + drawInfos.
		/// \param paramsBase       Push constants partagés (viewProj, cameraPos, time, screenSize).
		///                         Les champs per-instance (bottomColor, turbidity, flowDirection,
		///                         flowSpeed, refractionAmount, fresnelPower, reflectionStrength)
		///                         sont écrasés par Record selon scene.lakes/rivers[paramsIndex].
		/// \param scene            WaterScene pour récupérer les params per-instance.
		/// \param frameIndex       0..maxFrames-1.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent,
			ResourceId idSceneColorIn,
			ResourceId idSceneDepth,
			ResourceId idSceneColorOut,
			const WaterMeshGpu& mesh,
			const WaterPassPushConstants& paramsBase,
			const engine::world::water::WaterScene& scene,
			uint32_t frameIndex);

		/// Libère toutes les ressources Vulkan. Safe si non-init.
		void Destroy(VkDevice device);

		/// Détruit les framebuffers cachés (appeler au resize avant FG destroy).
		void InvalidateFramebufferCache(VkDevice device);

		/// Retourne true si le pipeline et le render pass sont valides.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
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

		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sceneColorSampler   = VK_NULL_HANDLE;  ///< Linear clamp pour sceneColor.
		VkSampler             m_sceneDepthSampler   = VK_NULL_HANDLE;  ///< Nearest clamp pour sceneDepth.

		// Vues externes mémorisées au Init (pour vkUpdateDescriptorSets en Record).
		VkImageView m_normalMapView    = VK_NULL_HANDLE;
		VkSampler   m_normalMapSampler = VK_NULL_HANDLE;
		VkImageView m_skyboxCubeView   = VK_NULL_HANDLE;
		VkSampler   m_skyboxSampler    = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_descriptorSets;  ///< 1 par frame en vol.
		std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> m_framebufferCache;
		uint32_t m_maxFrames = 2;
	};
}
