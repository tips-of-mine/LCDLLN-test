#include "engine/render/BrdfLutPass.h"
#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

namespace engine::render
{
	bool BrdfLutPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		uint32_t size,
		const uint32_t* compSpirv, size_t compWordCount,
		uint32_t queueFamilyIndex,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE
			|| !compSpirv || compWordCount == 0 || size == 0)
		{
			LOG_ERROR(Render, "BrdfLutPass::Init: invalid arguments");
			return false;
		}

		m_size        = size;
		m_vmaAllocator = vmaAllocator; // Conservé pour compat, plus utilisé pour l'alloc.

		// ---------------------------------------------------------------------
		// Image + memory (Vulkan brut, DEVICE_LOCAL)
		// ---------------------------------------------------------------------
		VkImageCreateInfo imgInfo{};
		imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.imageType     = VK_IMAGE_TYPE_2D;
		imgInfo.format        = VK_FORMAT_R16G16_SFLOAT;
		imgInfo.extent.width  = size;
		imgInfo.extent.height = size;
		imgInfo.extent.depth  = 1;
		imgInfo.mipLevels     = 1;
		imgInfo.arrayLayers   = 1;
		imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
		imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (vkCreateImage(device, &imgInfo, nullptr, &m_image) != VK_SUCCESS || m_image == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateImage failed");
			return false;
		}

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(device, m_image, &memReq);

		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

		auto findMemoryType = [&](uint32_t typeBits, VkMemoryPropertyFlags desiredFlags) -> uint32_t
		{
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((typeBits & (1u << i)) &&
					(memProps.memoryTypes[i].propertyFlags & desiredFlags) == desiredFlags)
				{
					return i;
				}
			}
			return UINT32_MAX;
		};

		const VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		uint32_t memTypeIdx = findMemoryType(memReq.memoryTypeBits, flags);
		if (memTypeIdx == UINT32_MAX)
		{
			LOG_ERROR(Render, "BrdfLutPass: no suitable DEVICE_LOCAL memory type");
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIdx;

		VkDeviceMemory memory = VK_NULL_HANDLE;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS || memory == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkAllocateMemory failed");
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		if (vkBindImageMemory(device, m_image, memory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkBindImageMemory failed");
			vkFreeMemory(device, memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		// On stocke VkDeviceMemory dans m_allocation (void*).
		m_allocation = reinterpret_cast<void*>(memory);

		// ---------------------------------------------------------------------
		// Image view + sampler
		// ---------------------------------------------------------------------
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &viewInfo, nullptr, &m_view) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateImageView failed");
			Destroy(device);
			return false;
		}

		VkSamplerCreateInfo sampInfo{};
		sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampInfo.magFilter = VK_FILTER_LINEAR;
		sampInfo.minFilter = VK_FILTER_LINEAR;
		sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampInfo.maxLod = 0.0f;

		if (vkCreateSampler(device, &sampInfo, nullptr, &m_sampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateSampler failed");
			Destroy(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// Descriptor set layout + pool + set
		// ---------------------------------------------------------------------
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
		setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setLayoutInfo.bindingCount = 1;
		setLayoutInfo.pBindings = &binding;

		if (vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &m_setLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateDescriptorSetLayout failed");
			Destroy(device);
			return false;
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSize.descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = 1;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateDescriptorPool failed");
			Destroy(device);
			return false;
		}

		VkDescriptorSetAllocateInfo descAllocInfo{};
		descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descAllocInfo.descriptorPool = m_descPool;
		descAllocInfo.descriptorSetCount = 1;
		descAllocInfo.pSetLayouts = &m_setLayout;

		if (vkAllocateDescriptorSets(device, &descAllocInfo, &m_descSet) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkAllocateDescriptorSets failed");
			Destroy(device);
			return false;
		}

		VkDescriptorImageInfo imgInfoDesc{};
		imgInfoDesc.imageView = m_view;
		imgInfoDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_descSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		write.pImageInfo = &imgInfoDesc;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

		// ---------------------------------------------------------------------
		// Pipeline layout + compute pipeline
		// ---------------------------------------------------------------------
		VkPipelineLayoutCreateInfo plInfo{};
		plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plInfo.setLayoutCount = 1;
		plInfo.pSetLayouts = &m_setLayout;

		if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreatePipelineLayout failed");
			Destroy(device);
			return false;
		}

		VkShaderModuleCreateInfo smInfo{};
		smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		smInfo.codeSize = compWordCount * sizeof(uint32_t);
		smInfo.pCode = compSpirv;

		VkShaderModule compModule = VK_NULL_HANDLE;
		if (vkCreateShaderModule(device, &smInfo, nullptr, &compModule) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateShaderModule (compute) failed");
			Destroy(device);
			return false;
		}

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = compModule;
		stageInfo.pName = "main";

		VkComputePipelineCreateInfo cpInfo{};
		cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		cpInfo.stage = stageInfo;
		cpInfo.layout = m_pipelineLayout;

		AssertPipelineCreationAllowed();
		PipelineCache::RegisterWarmupKey(HashComputePsoKey(m_pipelineLayout, compWordCount));
		if (vkCreateComputePipelines(device, pipelineCache, 1, &cpInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateComputePipelines failed");
			vkDestroyShaderModule(device, compModule, nullptr);
			Destroy(device);
			return false;
		}

		vkDestroyShaderModule(device, compModule, nullptr);

		// ---------------------------------------------------------------------
		// Command pool for one-off compute dispatch
		// ---------------------------------------------------------------------
		VkCommandPoolCreateInfo poolInfoCmd{};
		poolInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfoCmd.queueFamilyIndex = queueFamilyIndex;
		poolInfoCmd.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		if (vkCreateCommandPool(device, &poolInfoCmd, nullptr, &m_cmdPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "BrdfLutPass: vkCreateCommandPool failed");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "BrdfLutPass: initialised (size={}x{})", m_size, m_size);
		return true;
	}

	bool BrdfLutPass::Generate(VkDevice device, VkQueue queue)
	{
		if (device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE || m_cmdPool == VK_NULL_HANDLE)
			return false;

		VkCommandBufferAllocateInfo cmdBufAllocInfo{};
		cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdBufAllocInfo.commandPool = m_cmdPool;
		cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdBufAllocInfo.commandBufferCount = 1;

		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &cmd) != VK_SUCCESS)
			return false;

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
		{
			vkFreeCommandBuffers(device, m_cmdPool, 1, &cmd);
			return false;
		}

		// Transition image UNDEFINED -> GENERAL for storage write.
		VkImageMemoryBarrier barrierToGeneral{};
		barrierToGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrierToGeneral.srcAccessMask = 0;
		barrierToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrierToGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrierToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrierToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierToGeneral.image = m_image;
		barrierToGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrierToGeneral.subresourceRange.baseMipLevel = 0;
		barrierToGeneral.subresourceRange.levelCount = 1;
		barrierToGeneral.subresourceRange.baseArrayLayer = 0;
		barrierToGeneral.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrierToGeneral);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			m_pipelineLayout, 0, 1, &m_descSet, 0, nullptr);

		const uint32_t groupSize = 8u;
		const uint32_t groupCountX = (m_size + groupSize - 1u) / groupSize;
		const uint32_t groupCountY = (m_size + groupSize - 1u) / groupSize;

		vkCmdDispatch(cmd, groupCountX, groupCountY, 1u);

		// Transition image GENERAL -> SHADER_READ_ONLY_OPTIMAL for sampling.
		VkImageMemoryBarrier barrierToSample{};
		barrierToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrierToSample.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrierToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrierToSample.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrierToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrierToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierToSample.image = m_image;
		barrierToSample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrierToSample.subresourceRange.baseMipLevel = 0;
		barrierToSample.subresourceRange.levelCount = 1;
		barrierToSample.subresourceRange.baseArrayLayer = 0;
		barrierToSample.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrierToSample);

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			vkFreeCommandBuffers(device, m_cmdPool, 1, &cmd);
			return false;
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;

		if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			vkFreeCommandBuffers(device, m_cmdPool, 1, &cmd);
			return false;
		}

		vkQueueWaitIdle(queue);
		vkFreeCommandBuffers(device, m_cmdPool, 1, &cmd);

		LOG_INFO(Render, "BrdfLutPass: BRDF LUT generated");
		return true;
	}

	void BrdfLutPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (m_cmdPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device, m_cmdPool, nullptr);
			m_cmdPool = VK_NULL_HANDLE;
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
		if (m_descPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_descPool, nullptr);
			m_descPool = VK_NULL_HANDLE;
		}
		if (m_setLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
			m_setLayout = VK_NULL_HANDLE;
		}
		if (m_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_sampler, nullptr);
			m_sampler = VK_NULL_HANDLE;
		}
		if (m_view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_view, nullptr);
			m_view = VK_NULL_HANDLE;
		}
		if (m_image != VK_NULL_HANDLE && m_allocation != nullptr)
		{
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_allocation);
			vkDestroyImage(device, m_image, nullptr);
			vkFreeMemory(device, mem, nullptr);
			m_image      = VK_NULL_HANDLE;
			m_allocation = nullptr;
		}
		m_vmaAllocator = nullptr;
		m_size = 0;
	}
}

