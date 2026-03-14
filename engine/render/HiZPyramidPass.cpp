#include "engine/render/HiZPyramidPass.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>

namespace engine::render
{
	namespace
	{
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* spirv, size_t wordCount)
		{
			if (device == VK_NULL_HANDLE || !spirv || wordCount == 0)
				return VK_NULL_HANDLE;

			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = wordCount * sizeof(uint32_t);
			createInfo.pCode = spirv;

			VkShaderModule shaderModule = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
				return VK_NULL_HANDLE;
			return shaderModule;
		}

		uint32_t ComputeMipCount(VkExtent2D extent)
		{
			const uint32_t maxDim = std::max(extent.width, extent.height);
			if (maxDim == 0)
				return 0;

			uint32_t mipCount = 1;
			uint32_t dim = maxDim;
			while (dim > 1)
			{
				dim = std::max(1u, dim >> 1u);
				++mipCount;
			}
			return mipCount;
		}
	}

	bool HiZPyramidPass::CreateImage(VkDevice device, VkPhysicalDevice physicalDevice,
		uint32_t width, uint32_t height, uint32_t mipCount,
		VkImage& outImage, VkDeviceMemory& outMemory)
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = VK_FORMAT_R32_SFLOAT;
		imageInfo.extent = { width, height, 1u };
		imageInfo.mipLevels = mipCount;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Image create FAILED: vkCreateImage");
			return false;
		}

		VkMemoryRequirements memoryRequirements{};
		vkGetImageMemoryRequirements(device, outImage, &memoryRequirements);

		VkPhysicalDeviceMemoryProperties memoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

		uint32_t memoryTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			if ((memoryRequirements.memoryTypeBits & (1u << i)) != 0
				&& (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
			{
				memoryTypeIndex = i;
				break;
			}
		}

		if (memoryTypeIndex == UINT32_MAX)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Image create FAILED: no device-local memory");
			vkDestroyImage(device, outImage, nullptr);
			outImage = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Image create FAILED: vkAllocateMemory");
			vkDestroyImage(device, outImage, nullptr);
			outImage = VK_NULL_HANDLE;
			return false;
		}

		if (vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Image create FAILED: vkBindImageMemory");
			vkFreeMemory(device, outMemory, nullptr);
			vkDestroyImage(device, outImage, nullptr);
			outMemory = VK_NULL_HANDLE;
			outImage = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}

	VkImageView HiZPyramidPass::CreateImageView(VkDevice device, VkImage image, uint32_t baseMipLevel, uint32_t levelCount) const
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = baseMipLevel;
		viewInfo.subresourceRange.levelCount = levelCount;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView view = VK_NULL_HANDLE;
		if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		return view;
	}

	bool HiZPyramidPass::CreateImageResources(VkDevice device, VkPhysicalDevice physicalDevice, VkExtent2D extent)
	{
		if (extent.width == 0 || extent.height == 0)
		{
			LOG_WARN(Render, "[HiZPyramidPass] Resource create skipped: invalid extent {}x{}", extent.width, extent.height);
			return false;
		}

		DestroyImageResources(device);

		m_extent = extent;
		m_mipCount = ComputeMipCount(extent);
		m_slots.resize(m_framesInFlight);

		for (uint32_t frameSlotIndex = 0; frameSlotIndex < m_framesInFlight; ++frameSlotIndex)
		{
			FrameSlot& slot = m_slots[frameSlotIndex];
			if (!CreateImage(device, physicalDevice, extent.width, extent.height, m_mipCount, slot.image, slot.memory))
			{
				LOG_ERROR(Render, "[HiZPyramidPass] Resource create FAILED: image slot {}", frameSlotIndex);
				DestroyImageResources(device);
				return false;
			}

			slot.fullView = CreateImageView(device, slot.image, 0, m_mipCount);
			if (slot.fullView == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "[HiZPyramidPass] Resource create FAILED: full view slot {}", frameSlotIndex);
				DestroyImageResources(device);
				return false;
			}

			slot.mipViews.resize(m_mipCount);
			for (uint32_t mipLevel = 0; mipLevel < m_mipCount; ++mipLevel)
			{
				slot.mipViews[mipLevel] = CreateImageView(device, slot.image, mipLevel, 1u);
				if (slot.mipViews[mipLevel] == VK_NULL_HANDLE)
				{
					LOG_ERROR(Render, "[HiZPyramidPass] Resource create FAILED: mip view slot {} mip {}", frameSlotIndex, mipLevel);
					DestroyImageResources(device);
					return false;
				}
			}

			slot.hasValidData = false;
		}

		LOG_INFO(Render, "[HiZPyramidPass] Resources created (extent={}x{}, mips={}, frames_in_flight={})",
			extent.width, extent.height, m_mipCount, m_framesInFlight);
		return true;
	}

	void HiZPyramidPass::DestroyImageResources(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
			return;

		for (FrameSlot& slot : m_slots)
		{
			for (VkImageView mipView : slot.mipViews)
			{
				if (mipView != VK_NULL_HANDLE)
					vkDestroyImageView(device, mipView, nullptr);
			}
			slot.mipViews.clear();

			if (slot.fullView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(device, slot.fullView, nullptr);
				slot.fullView = VK_NULL_HANDLE;
			}
			if (slot.image != VK_NULL_HANDLE)
			{
				vkDestroyImage(device, slot.image, nullptr);
				slot.image = VK_NULL_HANDLE;
			}
			if (slot.memory != VK_NULL_HANDLE)
			{
				vkFreeMemory(device, slot.memory, nullptr);
				slot.memory = VK_NULL_HANDLE;
			}
			slot.hasValidData = false;
		}

		m_slots.clear();
		m_extent = { 0, 0 };
		m_mipCount = 0;
	}

	bool HiZPyramidPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		const uint32_t* computeSpirv, size_t computeWordCount,
		uint32_t framesInFlight)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || !computeSpirv || computeWordCount == 0 || framesInFlight == 0)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: invalid parameters");
			return false;
		}

		m_physicalDevice = physicalDevice;
		m_framesInFlight = framesInFlight;

		VkDescriptorSetLayoutBinding bindings[2]{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 2;
		layoutInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: descriptor set layout creation failed");
			Destroy(device);
			return false;
		}

		VkDescriptorPoolSize poolSizes[2]{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSizes[1].descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: descriptor pool creation failed");
			Destroy(device);
			return false;
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_descriptorSetLayout;
		if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: descriptor set allocation failed");
			Destroy(device);
			return false;
		}

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: pipeline layout creation failed");
			Destroy(device);
			return false;
		}

		VkShaderModule shaderModule = CreateShaderModule(device, computeSpirv, computeWordCount);
		if (shaderModule == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: shader module creation failed");
			Destroy(device);
			return false;
		}

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = m_pipelineLayout;
		pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipelineInfo.stage.module = shaderModule;
		pipelineInfo.stage.pName = "main";
		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			vkDestroyShaderModule(device, shaderModule, nullptr);
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: compute pipeline creation failed");
			Destroy(device);
			return false;
		}
		vkDestroyShaderModule(device, shaderModule, nullptr);

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 32.0f;
		if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[HiZPyramidPass] Init FAILED: sampler creation failed");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "[HiZPyramidPass] Init OK (frames_in_flight={})", m_framesInFlight);
		return true;
	}

	bool HiZPyramidPass::HasValidPyramid(uint32_t frameIndex) const
	{
		if (m_slots.empty() || m_framesInFlight == 0)
			return false;
		return m_slots[frameIndex % m_framesInFlight].hasValidData && m_slots[frameIndex % m_framesInFlight].fullView != VK_NULL_HANDLE;
	}

	VkImageView HiZPyramidPass::GetImageView(uint32_t frameIndex) const
	{
		if (!HasValidPyramid(frameIndex))
			return VK_NULL_HANDLE;
		return m_slots[frameIndex % m_framesInFlight].fullView;
	}

	void HiZPyramidPass::Record(VkDevice device, VkCommandBuffer cmd,
		VkImage depthImage, VkImageView depthView,
		VkExtent2D extent, uint32_t frameIndex)
	{
		if (!IsValid() || device == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE || depthImage == VK_NULL_HANDLE || depthView == VK_NULL_HANDLE)
			return;

		if ((m_extent.width != extent.width || m_extent.height != extent.height) || m_slots.empty())
		{
			if (!CreateImageResources(device, m_physicalDevice, extent))
			{
				LOG_WARN(Render, "[HiZPyramidPass] Record skipped: resource creation failed");
				return;
			}
		}

		FrameSlot& slot = m_slots[frameIndex % m_framesInFlight];
		if (slot.image == VK_NULL_HANDLE || slot.fullView == VK_NULL_HANDLE || slot.mipViews.size() != m_mipCount)
		{
			LOG_WARN(Render, "[HiZPyramidPass] Record skipped: slot resources unavailable");
			return;
		}

		VkImageMemoryBarrier imageToGeneral{};
		imageToGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageToGeneral.srcAccessMask = slot.hasValidData ? VK_ACCESS_SHADER_READ_BIT : 0;
		imageToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		imageToGeneral.oldLayout = slot.hasValidData ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
		imageToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageToGeneral.image = slot.image;
		imageToGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageToGeneral.subresourceRange.baseMipLevel = 0;
		imageToGeneral.subresourceRange.levelCount = m_mipCount;
		imageToGeneral.subresourceRange.baseArrayLayer = 0;
		imageToGeneral.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(cmd,
			slot.hasValidData ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &imageToGeneral);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

		for (uint32_t mipLevel = 0; mipLevel < m_mipCount; ++mipLevel)
		{
			const bool sourceIsDepth = (mipLevel == 0);
			const uint32_t srcWidth = sourceIsDepth ? extent.width : std::max(1u, extent.width >> (mipLevel - 1u));
			const uint32_t srcHeight = sourceIsDepth ? extent.height : std::max(1u, extent.height >> (mipLevel - 1u));
			const VkImageView sourceView = sourceIsDepth ? depthView : slot.fullView;
			const uint32_t sourceMipLevel = sourceIsDepth ? 0u : (mipLevel - 1u);

			VkDescriptorImageInfo sourceInfo{};
			sourceInfo.sampler = m_sampler;
			sourceInfo.imageView = sourceView;
			sourceInfo.imageLayout = sourceIsDepth ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;

			VkDescriptorImageInfo targetInfo{};
			targetInfo.imageView = slot.mipViews[mipLevel];
			targetInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet writes[2]{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = m_descriptorSet;
			writes[0].dstBinding = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[0].descriptorCount = 1;
			writes[0].pImageInfo = &sourceInfo;

			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = m_descriptorSet;
			writes[1].dstBinding = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			writes[1].descriptorCount = 1;
			writes[1].pImageInfo = &targetInfo;
			vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

			PushConstants pushConstants{};
			pushConstants.srcWidth = srcWidth;
			pushConstants.srcHeight = srcHeight;
			pushConstants.srcMipLevel = sourceMipLevel;
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pushConstants);

			const uint32_t dstWidth = std::max(1u, extent.width >> mipLevel);
			const uint32_t dstHeight = std::max(1u, extent.height >> mipLevel);
			vkCmdDispatch(cmd, (dstWidth + 7u) / 8u, (dstHeight + 7u) / 8u, 1u);

			VkImageMemoryBarrier mipBarrier{};
			mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			mipBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mipBarrier.image = slot.image;
			mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipBarrier.subresourceRange.baseMipLevel = mipLevel;
			mipBarrier.subresourceRange.levelCount = 1;
			mipBarrier.subresourceRange.baseArrayLayer = 0;
			mipBarrier.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &mipBarrier);
		}

		VkImageMemoryBarrier finalBarrier{};
		finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		finalBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		finalBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		finalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarrier.image = slot.image;
		finalBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		finalBarrier.subresourceRange.baseMipLevel = 0;
		finalBarrier.subresourceRange.levelCount = m_mipCount;
		finalBarrier.subresourceRange.baseArrayLayer = 0;
		finalBarrier.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &finalBarrier);

		slot.hasValidData = true;
	}

	void HiZPyramidPass::Destroy(VkDevice device)
	{
		if (device != VK_NULL_HANDLE)
		{
			DestroyImageResources(device);

			if (m_sampler != VK_NULL_HANDLE)
			{
				vkDestroySampler(device, m_sampler, nullptr);
				m_sampler = VK_NULL_HANDLE;
			}
			if (m_pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(device, m_pipeline, nullptr);
				m_pipeline = VK_NULL_HANDLE;
			}
			if (m_pipelineLayout != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				m_pipelineLayout = VK_NULL_HANDLE;
			}
			if (m_descriptorPool != VK_NULL_HANDLE)
			{
				vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
				m_descriptorPool = VK_NULL_HANDLE;
			}
			if (m_descriptorSetLayout != VK_NULL_HANDLE)
			{
				vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
				m_descriptorSetLayout = VK_NULL_HANDLE;
			}
		}

		m_descriptorSet = VK_NULL_HANDLE;
		m_physicalDevice = VK_NULL_HANDLE;
		m_framesInFlight = 0;
		LOG_INFO(Render, "[HiZPyramidPass] Destroyed");
	}
} // namespace engine::render
