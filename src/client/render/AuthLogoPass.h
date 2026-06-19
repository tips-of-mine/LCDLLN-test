#pragma once

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Textured quad (logo) avec rotation, pour l’overlay auth (dynamic rendering).
	class AuthLogoPass
	{
	public:
		AuthLogoPass() = default;
		AuthLogoPass(const AuthLogoPass&) = delete;
		AuthLogoPass& operator=(const AuthLogoPass&) = delete;

		bool Init(VkDevice device,
			VkFormat colorFormat,
			const uint32_t* vertSpirv,
			size_t vertWordCount,
			const uint32_t* fragSpirv,
			size_t fragWordCount,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// \p rotationRadians : angle d’animation « chargement » uniquement (effet 3D sur l’ampleur X).
		/// \p baseRotationRadians : rotation de base du quad (ex. π pour le logo connexion, 0 pour les petites icônes info).
		void Record(VkDevice device,
			VkCommandBuffer cmd,
			VkExtent2D extent,
			VkImage logoImage,
			VkImageView logoView,
			bool& inOutLayoutReady,
			float centerXPx,
			float centerYPx,
			float halfSizePx,
			float rotationRadians,
			float baseRotationRadians = 3.14159265f);

		void Destroy(VkDevice device);

		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		struct LogoPushConstants
		{
			float viewportSize[2];
			float centerPx[2];
			float halfExtentPx[2];
			float cosA;
			float sinA;
		};

		// Anneau de descriptor sets : Record() peut être appelé PLUSIEURS fois dans la
		// même frame (logo + icônes info + drapeaux FR/EN de l'écran de langue), et
		// chaque appel réécrit + lie + dessine. Avec un set UNIQUE, le 2e appel
		// réécrivait (vkUpdateDescriptorSets) le set encore référencé par le draw
		// précédent du même command buffer → usage Vulkan invalide → faute GPU sans
		// erreur CPU (crash déterministe sur l'écran de langue, jamais sur Login qui
		// ne dessine qu'un logo). On prend un set neuf à chaque Record via un curseur
		// circulaire. kDescriptorRingSize doit couvrir (draws/frame) × (frames in
		// flight) ; ~6 × 2 ici, 16 laisse une marge confortable.
		static constexpr uint32_t kDescriptorRingSize = 16u;

		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_descriptorSets[kDescriptorRingSize] = {};
		uint32_t m_descriptorCursor = 0u;
		VkSampler m_sampler = VK_NULL_HANDLE;
	};
}
