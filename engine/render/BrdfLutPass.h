#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace engine::render
{
	/// BrdfLutPass: generates a 2D BRDF integration LUT (R16G16, 256x256)
	/// for the split-sum GGX specular term using a compute shader.
	///
	/// The pass owns the Vulkan image, view, sampler, descriptor set and
	/// compute pipeline required to write the LUT once at engine startup.
	class BrdfLutPass
	{
	public:
		BrdfLutPass() = default;
		BrdfLutPass(const BrdfLutPass&) = delete;
		BrdfLutPass& operator=(const BrdfLutPass&) = delete;

		/// Initialises the BRDF LUT resources (image + view + sampler) and
		/// the compute pipeline from precompiled SPIR-V.
		/// \param size                LUT resolution (width=height=size, typically 256).
		/// \param compSpirv           Compute shader SPIR-V words.
		/// \param compWordCount       Number of 32-bit words in compSpirv.
		/// \param queueFamilyIndex    Queue family index used for the internal command pool.
		/// \return true on success.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			uint32_t size,
			const uint32_t* compSpirv, size_t compWordCount,
			uint32_t queueFamilyIndex);

		/// Generates the LUT once by recording and submitting a compute dispatch
		/// on the given queue. The function waits for completion before returning.
		/// \return true on success.
		bool Generate(VkDevice device, VkQueue queue);

		/// Destroys all Vulkan resources owned by the pass. Safe to call even
		/// when Init was not successful.
		void Destroy(VkDevice device);

		/// Returns the image view for sampling the LUT in later passes.
		VkImageView GetImageView() const { return m_view; }

		/// Returns the sampler used for sampling the LUT.
		VkSampler GetSampler() const { return m_sampler; }

	private:
		VkImage         m_image        = VK_NULL_HANDLE;
		VkDeviceMemory  m_memory       = VK_NULL_HANDLE;
		VkImageView     m_view         = VK_NULL_HANDLE;
		VkSampler       m_sampler      = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_setLayout   = VK_NULL_HANDLE;
		VkDescriptorPool      m_descPool    = VK_NULL_HANDLE;
		VkDescriptorSet       m_descSet     = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline            m_pipeline       = VK_NULL_HANDLE;
		VkCommandPool         m_cmdPool        = VK_NULL_HANDLE;
		uint32_t              m_size           = 0;
	};
}

