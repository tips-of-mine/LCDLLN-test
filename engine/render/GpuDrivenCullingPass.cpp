#include "engine/render/GpuDrivenCullingPass.h"
#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace engine::render
{
	namespace
	{
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* spirv, size_t wordCount)
		{
			if (device == VK_NULL_HANDLE || !spirv || wordCount == 0)
				return VK_NULL_HANDLE;

			VkShaderModuleCreateInfo smInfo{};
			smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			smInfo.codeSize = wordCount * sizeof(uint32_t);
			smInfo.pCode = spirv;

			VkShaderModule shaderModule = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &smInfo, nullptr, &shaderModule) != VK_SUCCESS)
				return VK_NULL_HANDLE;
			return shaderModule;
		}
	}

	bool GpuDrivenCullingPass::CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
		VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
		VkBuffer& outBuffer, VkDeviceMemory& outMemory)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || size == 0)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Buffer create FAILED: invalid parameters");
			return false;
		}

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Buffer create FAILED: vkCreateBuffer usage=0x{:x}", usage);
			return false;
		}

		VkMemoryRequirements memoryRequirements{};
		vkGetBufferMemoryRequirements(device, outBuffer, &memoryRequirements);

		VkPhysicalDeviceMemoryProperties memoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

		uint32_t memoryTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			if ((memoryRequirements.memoryTypeBits & (1u << i)) != 0
				&& (memoryProperties.memoryTypes[i].propertyFlags & memoryFlags) == memoryFlags)
			{
				memoryTypeIndex = i;
				break;
			}
		}

		if (memoryTypeIndex == UINT32_MAX)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Buffer create FAILED: no memory type for usage=0x{:x}", usage);
			vkDestroyBuffer(device, outBuffer, nullptr);
			outBuffer = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Buffer create FAILED: vkAllocateMemory");
			vkDestroyBuffer(device, outBuffer, nullptr);
			outBuffer = VK_NULL_HANDLE;
			return false;
		}

		if (vkBindBufferMemory(device, outBuffer, outMemory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Buffer create FAILED: vkBindBufferMemory");
			vkFreeMemory(device, outMemory, nullptr);
			vkDestroyBuffer(device, outBuffer, nullptr);
			outMemory = VK_NULL_HANDLE;
			outBuffer = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}

	bool GpuDrivenCullingPass::CreateFrameSlotBuffers(VkDevice device, VkPhysicalDevice physicalDevice, FrameSlot& slot)
	{
		const VkDeviceSize drawItemBytes = static_cast<VkDeviceSize>(m_maxDrawItems) * sizeof(GpuDrawItem);
		const VkDeviceSize visibleIndexBytes = static_cast<VkDeviceSize>(m_maxDrawItems) * sizeof(uint32_t);
		const VkDeviceSize visibleCountBytes = sizeof(uint32_t);
		const VkDeviceSize indirectBytes = static_cast<VkDeviceSize>(m_maxDrawItems) * sizeof(VkDrawIndexedIndirectCommand);
		const VkMemoryPropertyFlags hostVisibleCoherent =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		if (!CreateBuffer(device, physicalDevice,
				drawItemBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				hostVisibleCoherent,
				slot.drawItemBuffer, slot.drawItemMemory))
		{
			return false;
		}

		if (!CreateBuffer(device, physicalDevice,
				visibleIndexBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				hostVisibleCoherent,
				slot.visibleIndexBuffer, slot.visibleIndexMemory))
		{
			return false;
		}

		if (!CreateBuffer(device, physicalDevice,
				visibleCountBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				hostVisibleCoherent,
				slot.visibleCountBuffer, slot.visibleCountMemory))
		{
			return false;
		}

		if (!CreateBuffer(device, physicalDevice,
				indirectBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				hostVisibleCoherent,
				slot.indirectBuffer, slot.indirectMemory))
		{
			return false;
		}

		LOG_INFO(Render, "[GpuDrivenCullingPass] Frame slot buffers created (max_draw_items={})", m_maxDrawItems);
		return true;
	}

	bool GpuDrivenCullingPass::CreateFallbackImage(VkDevice device, VkPhysicalDevice physicalDevice)
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = VK_FORMAT_R32_SFLOAT;
		imageInfo.extent = { 1u, 1u, 1u };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vkCreateImage(device, &imageInfo, nullptr, &m_fallbackHiZImage) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: fallback Hi-Z image creation failed");
			return false;
		}

		VkMemoryRequirements memoryRequirements{};
		vkGetImageMemoryRequirements(device, m_fallbackHiZImage, &memoryRequirements);

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
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: fallback Hi-Z memory type not found");
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_fallbackHiZMemory) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: fallback Hi-Z memory allocation failed");
			return false;
		}

		if (vkBindImageMemory(device, m_fallbackHiZImage, m_fallbackHiZMemory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: fallback Hi-Z bind memory failed");
			return false;
		}

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_fallbackHiZImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_fallbackHiZView) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: fallback Hi-Z view creation failed");
			return false;
		}

		return true;
	}

	bool GpuDrivenCullingPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		const uint32_t* computeSpirv, size_t computeWordCount,
		uint32_t framesInFlight, uint32_t maxDrawItems,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE
			|| !computeSpirv || computeWordCount == 0
			|| framesInFlight == 0 || maxDrawItems == 0)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: invalid parameters");
			return false;
		}

		m_framesInFlight = framesInFlight;
		m_maxDrawItems = maxDrawItems;
		m_slots = new (std::nothrow) FrameSlot[m_framesInFlight];
		if (!m_slots)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: frame slot allocation failed");
			return false;
		}

		VkDescriptorSetLayoutBinding bindings[5]{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[3].binding = 3;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[3].descriptorCount = 1;
		bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[4].binding = 4;
		bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[4].descriptorCount = 1;
		bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 5;
		layoutInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: descriptor set layout creation failed");
			Destroy(device);
			return false;
		}

		VkDescriptorPoolSize poolSizes[2]{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[0].descriptorCount = m_framesInFlight * 4u;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = m_framesInFlight;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = m_framesInFlight;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: descriptor pool creation failed");
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
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: pipeline layout creation failed");
			Destroy(device);
			return false;
		}

		VkShaderModule shaderModule = CreateShaderModule(device, computeSpirv, computeWordCount);
		if (shaderModule == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: compute shader module creation failed");
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

		AssertPipelineCreationAllowed();
		PipelineCache::RegisterWarmupKey(HashComputePsoKey(m_pipelineLayout, computeWordCount));
		if (vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			vkDestroyShaderModule(device, shaderModule, nullptr);
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: compute pipeline creation failed");
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
		if (vkCreateSampler(device, &samplerInfo, nullptr, &m_hiZSampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: Hi-Z sampler creation failed");
			Destroy(device);
			return false;
		}

		if (!CreateFallbackImage(device, physicalDevice))
		{
			Destroy(device);
			return false;
		}

		for (uint32_t slotIndex = 0; slotIndex < m_framesInFlight; ++slotIndex)
		{
			if (!CreateFrameSlotBuffers(device, physicalDevice, m_slots[slotIndex]))
			{
				LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: frame slot {} buffer creation failed", slotIndex);
				Destroy(device);
				return false;
			}
		}

		VkDescriptorSetLayout* layouts = new (std::nothrow) VkDescriptorSetLayout[m_framesInFlight];
		if (!layouts)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: descriptor layout array allocation failed");
			Destroy(device);
			return false;
		}
		for (uint32_t i = 0; i < m_framesInFlight; ++i)
			layouts[i] = m_descriptorSetLayout;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = m_framesInFlight;
		allocInfo.pSetLayouts = layouts;

		VkDescriptorSet* descriptorSets = new (std::nothrow) VkDescriptorSet[m_framesInFlight];
		if (!descriptorSets)
		{
			delete[] layouts;
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: descriptor set array allocation failed");
			Destroy(device);
			return false;
		}

		if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets) != VK_SUCCESS)
		{
			delete[] descriptorSets;
			delete[] layouts;
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: descriptor set allocation failed");
			Destroy(device);
			return false;
		}
		delete[] layouts;

		for (uint32_t slotIndex = 0; slotIndex < m_framesInFlight; ++slotIndex)
		{
			FrameSlot& slot = m_slots[slotIndex];
			slot.descriptorSet = descriptorSets[slotIndex];

			VkDescriptorBufferInfo drawItemInfo{};
			drawItemInfo.buffer = slot.drawItemBuffer;
			drawItemInfo.offset = 0;
			drawItemInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo visibleIndexInfo{};
			visibleIndexInfo.buffer = slot.visibleIndexBuffer;
			visibleIndexInfo.offset = 0;
			visibleIndexInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo visibleCountInfo{};
			visibleCountInfo.buffer = slot.visibleCountBuffer;
			visibleCountInfo.offset = 0;
			visibleCountInfo.range = sizeof(uint32_t);

			VkDescriptorBufferInfo indirectInfo{};
			indirectInfo.buffer = slot.indirectBuffer;
			indirectInfo.offset = 0;
			indirectInfo.range = VK_WHOLE_SIZE;

			VkDescriptorImageInfo hiZInfo{};
			hiZInfo.sampler = m_hiZSampler;
			hiZInfo.imageView = m_fallbackHiZView;
			hiZInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet writes[5]{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = slot.descriptorSet;
			writes[0].dstBinding = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[0].descriptorCount = 1;
			writes[0].pBufferInfo = &drawItemInfo;

			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = slot.descriptorSet;
			writes[1].dstBinding = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[1].descriptorCount = 1;
			writes[1].pBufferInfo = &visibleIndexInfo;

			writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[2].dstSet = slot.descriptorSet;
			writes[2].dstBinding = 2;
			writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[2].descriptorCount = 1;
			writes[2].pBufferInfo = &visibleCountInfo;

			writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[3].dstSet = slot.descriptorSet;
			writes[3].dstBinding = 3;
			writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[3].descriptorCount = 1;
			writes[3].pBufferInfo = &indirectInfo;

			writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[4].dstSet = slot.descriptorSet;
			writes[4].dstBinding = 4;
			writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[4].descriptorCount = 1;
			writes[4].pImageInfo = &hiZInfo;

			vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
		}
		delete[] descriptorSets;

		LOG_INFO(Render, "[GpuDrivenCullingPass] Init OK (frames_in_flight={}, max_draw_items={})",
			m_framesInFlight, m_maxDrawItems);
		return true;
	}

	bool GpuDrivenCullingPass::UploadDrawItems(VkDevice device, uint32_t frameIndex, const GpuDrawItem* items, uint32_t itemCount)
	{
		if (!IsValid() || device == VK_NULL_HANDLE || !m_slots)
		{
			LOG_WARN(Render, "[GpuDrivenCullingPass] Upload skipped: pass not initialized");
			return false;
		}

		if (itemCount > m_maxDrawItems)
		{
			LOG_WARN(Render, "[GpuDrivenCullingPass] Upload truncated: {} -> {}", itemCount, m_maxDrawItems);
			itemCount = m_maxDrawItems;
		}

		FrameSlot& slot = m_slots[frameIndex % m_framesInFlight];
		slot.uploadedDrawItemCount = itemCount;
		if (itemCount == 0)
			return true;
		if (items == nullptr)
		{
			slot.uploadedDrawItemCount = 0;
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Upload FAILED: null draw-item pointer");
			return false;
		}

		void* mapped = nullptr;
		if (vkMapMemory(device, slot.drawItemMemory, 0,
				static_cast<VkDeviceSize>(itemCount) * sizeof(GpuDrawItem), 0, &mapped) != VK_SUCCESS
			|| mapped == nullptr)
		{
			slot.uploadedDrawItemCount = 0;
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Upload FAILED: draw-item buffer map failed");
			return false;
		}

		std::memcpy(mapped, items, static_cast<size_t>(itemCount) * sizeof(GpuDrawItem));
		vkUnmapMemory(device, slot.drawItemMemory);
		return true;
	}

	void GpuDrivenCullingPass::Record(VkDevice device, VkCommandBuffer cmd, const float* viewProjMatrix4x4, uint32_t frameIndex,
		VkImageView hiZImageView, VkExtent2D hiZExtent, uint32_t hiZMipCount)
	{
		if (!IsValid() || device == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE || !viewProjMatrix4x4 || !m_slots)
			return;

		FrameSlot& slot = m_slots[frameIndex % m_framesInFlight];
		const bool occlusionEnabled = hiZImageView != VK_NULL_HANDLE && hiZMipCount > 0
			&& hiZExtent.width > 0 && hiZExtent.height > 0;

		if (!occlusionEnabled && !m_fallbackHiZReady)
		{
			VkImageMemoryBarrier toTransfer{};
			toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toTransfer.srcAccessMask = 0;
			toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.image = m_fallbackHiZImage;
			toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			toTransfer.subresourceRange.baseMipLevel = 0;
			toTransfer.subresourceRange.levelCount = 1;
			toTransfer.subresourceRange.baseArrayLayer = 0;
			toTransfer.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &toTransfer);

			VkClearColorValue clearColor{};
			clearColor.float32[0] = 1.0f;
			clearColor.float32[1] = 0.0f;
			clearColor.float32[2] = 0.0f;
			clearColor.float32[3] = 1.0f;
			VkImageSubresourceRange clearRange{};
			clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			clearRange.baseMipLevel = 0;
			clearRange.levelCount = 1;
			clearRange.baseArrayLayer = 0;
			clearRange.layerCount = 1;
			vkCmdClearColorImage(cmd, m_fallbackHiZImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

			VkImageMemoryBarrier toSample{};
			toSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSample.image = m_fallbackHiZImage;
			toSample.subresourceRange = clearRange;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &toSample);
			m_fallbackHiZReady = true;
			LOG_INFO(Render, "[GpuDrivenCullingPass] Conservative fallback Hi-Z initialized");
		}

		VkDescriptorImageInfo hiZInfo{};
		hiZInfo.sampler = m_hiZSampler;
		hiZInfo.imageView = occlusionEnabled ? hiZImageView : m_fallbackHiZView;
		hiZInfo.imageLayout = occlusionEnabled ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet hiZWrite{};
		hiZWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		hiZWrite.dstSet = slot.descriptorSet;
		hiZWrite.dstBinding = 4;
		hiZWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		hiZWrite.descriptorCount = 1;
		hiZWrite.pImageInfo = &hiZInfo;
		vkUpdateDescriptorSets(device, 1, &hiZWrite, 0, nullptr);

		vkCmdFillBuffer(cmd, slot.visibleCountBuffer, 0, sizeof(uint32_t), 0u);

		VkBufferMemoryBarrier resetBarrier{};
		resetBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		resetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		resetBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		resetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		resetBarrier.buffer = slot.visibleCountBuffer;
		resetBarrier.offset = 0;
		resetBarrier.size = sizeof(uint32_t);
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 1, &resetBarrier, 0, nullptr);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &slot.descriptorSet, 0, nullptr);

		PushConstants pushConstants{};
		std::memcpy(pushConstants.viewProj, viewProjMatrix4x4, sizeof(pushConstants.viewProj));
		pushConstants.drawItemCount = slot.uploadedDrawItemCount;
		pushConstants.maxDrawItems = m_maxDrawItems;
		pushConstants.hiZMipCount = occlusionEnabled ? hiZMipCount : 0u;
		pushConstants.occlusionEnabled = occlusionEnabled ? 1u : 0u;
		pushConstants.hiZWidth = static_cast<float>(occlusionEnabled ? hiZExtent.width : 1u);
		pushConstants.hiZHeight = static_cast<float>(occlusionEnabled ? hiZExtent.height : 1u);
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pushConstants);

		const uint32_t groupCountX = std::max(1u, (slot.uploadedDrawItemCount + 63u) / 64u);
		vkCmdDispatch(cmd, groupCountX, 1u, 1u);

		VkBufferMemoryBarrier drawItemBarrier{};
		drawItemBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		drawItemBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		drawItemBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		drawItemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		drawItemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		drawItemBarrier.buffer = slot.indirectBuffer;
		drawItemBarrier.offset = 0;
		drawItemBarrier.size = VK_WHOLE_SIZE;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
			0, 0, nullptr, 1, &drawItemBarrier, 0, nullptr);
	}

	VkBuffer GpuDrivenCullingPass::GetIndirectBuffer(uint32_t frameIndex) const
	{
		if (!m_slots || m_framesInFlight == 0)
			return VK_NULL_HANDLE;
		return m_slots[frameIndex % m_framesInFlight].indirectBuffer;
	}

	uint32_t GpuDrivenCullingPass::GetDrawItemCount(uint32_t frameIndex) const
	{
		if (!m_slots || m_framesInFlight == 0)
			return 0;
		return m_slots[frameIndex % m_framesInFlight].uploadedDrawItemCount;
	}

	void GpuDrivenCullingPass::DestroyFrameSlot(VkDevice device, FrameSlot& slot)
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (slot.indirectBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, slot.indirectBuffer, nullptr);
			slot.indirectBuffer = VK_NULL_HANDLE;
		}
		if (slot.indirectMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, slot.indirectMemory, nullptr);
			slot.indirectMemory = VK_NULL_HANDLE;
		}
		if (slot.visibleCountBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, slot.visibleCountBuffer, nullptr);
			slot.visibleCountBuffer = VK_NULL_HANDLE;
		}
		if (slot.visibleCountMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, slot.visibleCountMemory, nullptr);
			slot.visibleCountMemory = VK_NULL_HANDLE;
		}
		if (slot.visibleIndexBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, slot.visibleIndexBuffer, nullptr);
			slot.visibleIndexBuffer = VK_NULL_HANDLE;
		}
		if (slot.visibleIndexMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, slot.visibleIndexMemory, nullptr);
			slot.visibleIndexMemory = VK_NULL_HANDLE;
		}
		if (slot.drawItemBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, slot.drawItemBuffer, nullptr);
			slot.drawItemBuffer = VK_NULL_HANDLE;
		}
		if (slot.drawItemMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, slot.drawItemMemory, nullptr);
			slot.drawItemMemory = VK_NULL_HANDLE;
		}
		slot.descriptorSet = VK_NULL_HANDLE;
		slot.uploadedDrawItemCount = 0;
	}

	void GpuDrivenCullingPass::Destroy(VkDevice device)
	{
		if (device != VK_NULL_HANDLE && m_slots != nullptr)
		{
			for (uint32_t i = 0; i < m_framesInFlight; ++i)
				DestroyFrameSlot(device, m_slots[i]);
		}

		delete[] m_slots;
		m_slots = nullptr;

		if (device != VK_NULL_HANDLE)
		{
			if (m_hiZSampler != VK_NULL_HANDLE)
			{
				vkDestroySampler(device, m_hiZSampler, nullptr);
				m_hiZSampler = VK_NULL_HANDLE;
			}
			if (m_fallbackHiZView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(device, m_fallbackHiZView, nullptr);
				m_fallbackHiZView = VK_NULL_HANDLE;
			}
			if (m_fallbackHiZImage != VK_NULL_HANDLE)
			{
				vkDestroyImage(device, m_fallbackHiZImage, nullptr);
				m_fallbackHiZImage = VK_NULL_HANDLE;
			}
			if (m_fallbackHiZMemory != VK_NULL_HANDLE)
			{
				vkFreeMemory(device, m_fallbackHiZMemory, nullptr);
				m_fallbackHiZMemory = VK_NULL_HANDLE;
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

		m_framesInFlight = 0;
		m_maxDrawItems = 0;
		m_fallbackHiZReady = false;
		LOG_INFO(Render, "[GpuDrivenCullingPass] Destroyed");
	}
} // namespace engine::render
