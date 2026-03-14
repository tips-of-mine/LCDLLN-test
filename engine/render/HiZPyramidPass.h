#pragma once

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Builds a per-frame Hi-Z depth pyramid from the scene depth buffer.
	/// The pyramid is later sampled by GPU culling on a conservative fallback path.
	class HiZPyramidPass
	{
	public:
		static constexpr uint32_t kDefaultFramesInFlight = 2u;

		HiZPyramidPass() = default;
		HiZPyramidPass(const HiZPyramidPass&) = delete;
		HiZPyramidPass& operator=(const HiZPyramidPass&) = delete;

		/// Creates the compute pipeline and the sampler used to read depth / Hi-Z mips.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			const uint32_t* computeSpirv, size_t computeWordCount,
			uint32_t framesInFlight = kDefaultFramesInFlight);

		/// Builds the pyramid for the current frame slot from the provided depth image.
		void Record(VkDevice device, VkCommandBuffer cmd,
			VkImage depthImage, VkImageView depthView,
			VkExtent2D extent, uint32_t frameIndex);

		/// Releases all resources. Safe to call when not initialized.
		void Destroy(VkDevice device);

		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }
		bool HasValidPyramid(uint32_t frameIndex) const;
		VkImageView GetImageView(uint32_t frameIndex) const;
		VkSampler GetSampler() const { return m_sampler; }
		uint32_t GetMipCount() const { return m_mipCount; }
		VkExtent2D GetExtent() const { return m_extent; }

	private:
		struct PushConstants
		{
			uint32_t srcWidth = 0;
			uint32_t srcHeight = 0;
			uint32_t srcMipLevel = 0;
			uint32_t reserved0 = 0;
		};

		struct FrameSlot
		{
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView fullView = VK_NULL_HANDLE;
			std::vector<VkImageView> mipViews;
			bool hasValidData = false;
		};

		bool CreateImageResources(VkDevice device, VkPhysicalDevice physicalDevice, VkExtent2D extent);
		void DestroyImageResources(VkDevice device);
		bool CreateImage(VkDevice device, VkPhysicalDevice physicalDevice,
			uint32_t width, uint32_t height, uint32_t mipCount,
			VkImage& outImage, VkDeviceMemory& outMemory);
		VkImageView CreateImageView(VkDevice device, VkImage image, uint32_t baseMipLevel, uint32_t levelCount) const;

		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkSampler m_sampler = VK_NULL_HANDLE;
		uint32_t m_framesInFlight = 0;
		uint32_t m_mipCount = 0;
		VkExtent2D m_extent = { 0, 0 };
		std::vector<FrameSlot> m_slots;
	};
} // namespace engine::render
