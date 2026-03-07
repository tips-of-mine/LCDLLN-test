#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace engine::render
{
	/// SpecularPrefilterPass (M05.3): generates a prefiltered specular cubemap
	/// (GGX, roughness per mip) from a source HDR environment cubemap.
	///
	/// Roughness maps to mip level: roughness = mipLevel / (mipCount - 1).
	/// Output: RGBA16F cubemap (256 or 512 per face) with mip chain, suitable
	/// for split-sum IBL specular. Generate() requires a valid source cubemap
	/// view and sampler; when unavailable, Generate() returns false and the
	/// pass holds no prefiltered data.
	class SpecularPrefilterPass
	{
	public:
		SpecularPrefilterPass() = default;
		SpecularPrefilterPass(const SpecularPrefilterPass&) = delete;
		SpecularPrefilterPass& operator=(const SpecularPrefilterPass&) = delete;

		/// Initialises the output cubemap (with mips), descriptor layout/pool,
		/// and compute pipeline. Does not require a source cubemap.
		/// \param size              Base face size (e.g. 256 or 512).
		/// \param mipCount          Number of mip levels (roughness steps).
		/// \param compSpirv        Compute shader SPIR-V.
		/// \param compWordCount    Number of 32-bit words in compSpirv.
		/// \return true on success.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			uint32_t size, uint32_t mipCount,
			const uint32_t* compSpirv, size_t compWordCount,
			uint32_t queueFamilyIndex);

		/// Runs the prefilter: for each mip (roughness), dispatches compute per
		/// face, then transitions the cubemap to SHADER_READ_ONLY_OPTIMAL.
		/// \return true on success; false if source view/sampler are null or
		///         submission fails.
		bool Generate(VkDevice device, VkQueue queue,
			VkImageView sourceCubemapView, VkSampler sourceCubemapSampler);

		/// Releases all Vulkan resources. Safe to call even if Init failed.
		void Destroy(VkDevice device);

		/// Cubemap view covering all 6 faces and all mip levels (for sampling).
		VkImageView GetImageView() const { return m_cubeView; }

		/// Sampler for the prefiltered cubemap (linear, with mip filtering).
		VkSampler GetSampler() const { return m_sampler; }

		/// Whether the pass was initialised and can be used (Generate may still
		/// not have been run if no source cubemap was provided).
		bool IsValid() const { return m_image != VK_NULL_HANDLE && m_cubeView != VK_NULL_HANDLE; }

	private:
		VkImage                    m_image       = VK_NULL_HANDLE;
		VkDeviceMemory             m_memory       = VK_NULL_HANDLE;
		VkImageView                m_cubeView     = VK_NULL_HANDLE;
		VkSampler                  m_sampler     = VK_NULL_HANDLE;
		std::vector<VkImageView>   m_faceMipViews;  // 6 * mipCount views for write
		VkDescriptorSetLayout      m_setLayout    = VK_NULL_HANDLE;
		VkDescriptorPool           m_descPool    = VK_NULL_HANDLE;
		VkDescriptorSet            m_descSet     = VK_NULL_HANDLE;
		VkPipelineLayout           m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline                 m_pipeline    = VK_NULL_HANDLE;
		VkCommandPool              m_cmdPool     = VK_NULL_HANDLE;
		uint32_t                   m_size        = 0;
		uint32_t                   m_mipCount    = 0;
	};
}
