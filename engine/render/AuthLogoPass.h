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

		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
		VkSampler m_sampler = VK_NULL_HANDLE;
	};
}
