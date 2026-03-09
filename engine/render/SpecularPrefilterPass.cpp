#include "engine/render/SpecularPrefilterPass.h"

#include "engine/core/Log.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstring>

namespace engine::render
{
	namespace
	{
		/// Push constant layout matching specular_prefilter.comp (face, faceSize, roughness, pad).
		struct SpecularPrefilterPushConstants
		{
			uint32_t face;
			uint32_t faceSize;
			float roughness;
			float pad;
		};
	}

	bool SpecularPrefilterPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		uint32_t size, uint32_t mipCount,
		const uint32_t* compSpirv, size_t compWordCount,
		uint32_t queueFamilyIndex)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || vmaAllocator == nullptr
			|| !compSpirv || compWordCount == 0 || size == 0 || mipCount == 0)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass::Init: invalid arguments");
			return false;
		}

		m_size = size;
		m_mipCount = mipCount;
		m_vmaAllocator = vmaAllocator;
		VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator);

		// ---------------------------------------------------------------------
		// Cubemap image (6 layers, mipCount levels) — VMA
		// ---------------------------------------------------------------------
		VkImageCreateInfo imgInfo{};
		imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		imgInfo.imageType = VK_IMAGE_TYPE_2D;
		imgInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		imgInfo.extent.width = size;
		imgInfo.extent.height = size;
		imgInfo.extent.depth = 1;
		imgInfo.mipLevels = mipCount;
		imgInfo.arrayLayers = 6;
		imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imgInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocCreateInfo{};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		VmaAllocation imgAlloc = VK_NULL_HANDLE;
		if (vmaCreateImage(alloc, &imgInfo, &allocCreateInfo, &m_image, &imgAlloc, nullptr) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vmaCreateImage failed");
			return false;
		}
		m_allocation = imgAlloc;

		// ---------------------------------------------------------------------
		// Full cubemap view (sampling) + per-face-per-mip views (storage write)
		// ---------------------------------------------------------------------
		VkImageViewCreateInfo cubeViewInfo{};
		cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		cubeViewInfo.image = m_image;
		cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		cubeViewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		cubeViewInfo.subresourceRange.baseMipLevel = 0;
		cubeViewInfo.subresourceRange.levelCount = mipCount;
		cubeViewInfo.subresourceRange.baseArrayLayer = 0;
		cubeViewInfo.subresourceRange.layerCount = 6;
		if (vkCreateImageView(device, &cubeViewInfo, nullptr, &m_cubeView) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateImageView (cube) failed");
			Destroy(device);
			return false;
		}

		m_faceMipViews.reserve(6u * mipCount);
		for (uint32_t face = 0; face < 6u; ++face)
		{
			for (uint32_t mip = 0; mip < mipCount; ++mip)
			{
				VkImageViewCreateInfo viewInfo{};
				viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewInfo.image = m_image;
				viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
				viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewInfo.subresourceRange.baseMipLevel = mip;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.baseArrayLayer = face;
				viewInfo.subresourceRange.layerCount = 1;
				VkImageView view = VK_NULL_HANDLE;
				if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
				{
					LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateImageView (face/mip) failed");
					Destroy(device);
					return false;
				}
				m_faceMipViews.push_back(view);
			}
		}

		// ---------------------------------------------------------------------
		// Sampler (linear, with mip filtering)
		// ---------------------------------------------------------------------
		VkSamplerCreateInfo sampInfo{};
		sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampInfo.magFilter = VK_FILTER_LINEAR;
		sampInfo.minFilter = VK_FILTER_LINEAR;
		sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampInfo.maxLod = static_cast<float>(mipCount);
		if (vkCreateSampler(device, &sampInfo, nullptr, &m_sampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateSampler failed");
			Destroy(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// Descriptor set layout: binding 0 = cubemap sampler, 1 = storage image
		// ---------------------------------------------------------------------
		VkDescriptorSetLayoutBinding bindings[2]{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
		setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setLayoutInfo.bindingCount = 2;
		setLayoutInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &m_setLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateDescriptorSetLayout failed");
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
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		poolInfo.maxSets = 1;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateDescriptorPool failed");
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
			LOG_ERROR(Render, "SpecularPrefilterPass: vkAllocateDescriptorSets failed");
			Destroy(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// Push constants + pipeline layout
		// ---------------------------------------------------------------------
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(SpecularPrefilterPushConstants);
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &m_setLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreatePipelineLayout failed");
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
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateShaderModule failed");
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
		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateComputePipelines failed");
			vkDestroyShaderModule(device, compModule, nullptr);
			Destroy(device);
			return false;
		}
		vkDestroyShaderModule(device, compModule, nullptr);

		VkCommandPoolCreateInfo poolInfoCmd{};
		poolInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfoCmd.queueFamilyIndex = queueFamilyIndex;
		poolInfoCmd.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		if (vkCreateCommandPool(device, &poolInfoCmd, nullptr, &m_cmdPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SpecularPrefilterPass: vkCreateCommandPool failed");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "SpecularPrefilterPass: initialised (size={}, mips={})", m_size, m_mipCount);
		return true;
	}

	bool SpecularPrefilterPass::Generate(VkDevice device, VkQueue queue,
		VkImageView sourceCubemapView, VkSampler sourceCubemapSampler)
	{
		if (device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE || m_cmdPool == VK_NULL_HANDLE
			|| sourceCubemapView == VK_NULL_HANDLE || sourceCubemapSampler == VK_NULL_HANDLE)
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

		const uint32_t groupSize = 8u;
		SpecularPrefilterPushConstants pc{};

		for (uint32_t mip = 0; mip < m_mipCount; ++mip)
		{
			const uint32_t faceSize = (m_size >> mip) > 0u ? (m_size >> mip) : 1u;
			pc.roughness = (m_mipCount > 1u) ? (static_cast<float>(mip) / static_cast<float>(m_mipCount - 1u)) : 0.0f;
			pc.pad = 0.0f;

			for (uint32_t face = 0; face < 6u; ++face)
			{
				pc.face = face;
				pc.faceSize = faceSize;

				// Update descriptor: source cubemap + output face/mip view
				VkDescriptorImageInfo srcImgInfo{};
				srcImgInfo.sampler = sourceCubemapSampler;
				srcImgInfo.imageView = sourceCubemapView;
				srcImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				VkDescriptorImageInfo outImgInfo{};
				outImgInfo.imageView = m_faceMipViews[face * m_mipCount + mip];
				outImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

				VkWriteDescriptorSet writes[2]{};
				writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[0].dstSet = m_descSet;
				writes[0].dstBinding = 0;
				writes[0].descriptorCount = 1;
				writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[0].pImageInfo = &srcImgInfo;
				writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[1].dstSet = m_descSet;
				writes[1].dstBinding = 1;
				writes[1].descriptorCount = 1;
				writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				writes[1].pImageInfo = &outImgInfo;
				vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

				// Transition this face/mip to GENERAL for write
				VkImageMemoryBarrier toGeneral{};
				toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toGeneral.srcAccessMask = 0;
				toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				toGeneral.image = m_image;
				toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				toGeneral.subresourceRange.baseMipLevel = mip;
				toGeneral.subresourceRange.levelCount = 1;
				toGeneral.subresourceRange.baseArrayLayer = face;
				toGeneral.subresourceRange.layerCount = 1;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &toGeneral);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
					m_pipelineLayout, 0, 1, &m_descSet, 0, nullptr);
				vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
					sizeof(SpecularPrefilterPushConstants), &pc);

				const uint32_t groupsX = (faceSize + groupSize - 1u) / groupSize;
				const uint32_t groupsY = (faceSize + groupSize - 1u) / groupSize;
				vkCmdDispatch(cmd, groupsX, groupsY, 1u);

				// Barrier so next face/mip or final transition sees the write
				VkImageMemoryBarrier afterWrite{};
				afterWrite.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				afterWrite.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				afterWrite.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				afterWrite.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
				afterWrite.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				afterWrite.image = m_image;
				afterWrite.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				afterWrite.subresourceRange.baseMipLevel = mip;
				afterWrite.subresourceRange.levelCount = 1;
				afterWrite.subresourceRange.baseArrayLayer = face;
				afterWrite.subresourceRange.layerCount = 1;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &afterWrite);
			}
		}

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

		LOG_INFO(Render, "SpecularPrefilterPass: prefiltered cubemap generated");
		return true;
	}

	void SpecularPrefilterPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
			return;
		for (VkImageView v : m_faceMipViews)
		{
			if (v != VK_NULL_HANDLE)
				vkDestroyImageView(device, v, nullptr);
		}
		m_faceMipViews.clear();
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
		if (m_cubeView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_cubeView, nullptr);
			m_cubeView = VK_NULL_HANDLE;
		}
		if (m_image != VK_NULL_HANDLE && m_allocation != nullptr && m_vmaAllocator != nullptr)
		{
			vmaDestroyImage(static_cast<VmaAllocator>(m_vmaAllocator), m_image, static_cast<VmaAllocation>(m_allocation));
			m_image = VK_NULL_HANDLE;
			m_allocation = nullptr;
		}
		m_vmaAllocator = nullptr;
		m_size = 0;
		m_mipCount = 0;
	}
}
