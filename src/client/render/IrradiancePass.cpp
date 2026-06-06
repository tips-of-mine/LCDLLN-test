#include "src/client/render/IrradiancePass.h"
#include "src/client/render/PipelineCache.h"

#include "src/shared/core/Log.h"

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>

namespace engine::render
{
	namespace
	{
		/// Layout des push-constants miroir de irradiance_convolve.comp
		/// (face, faceSize, pad0, pad1). 16 octets, std430.
		struct IrradiancePushConstants
		{
			uint32_t face;
			uint32_t faceSize;
			float pad0;
			float pad1;
		};
		static_assert(sizeof(IrradiancePushConstants) == 16,
			"IrradiancePushConstants doit faire 16 octets (layout std430 du shader)");
	}

	bool IrradiancePass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		uint32_t faceSize,
		const uint32_t* compSpirv, size_t compWordCount,
		uint32_t queueFamilyIndex,
		VkPipelineCache pipelineCache)
	{
		LOG_INFO(Render, "[IRRAD] Init enter size={}", faceSize);
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE
			|| !compSpirv || compWordCount == 0 || faceSize == 0)
		{
			LOG_ERROR(Render, "IrradiancePass::Init: invalid arguments");
			return false;
		}

		m_size = faceSize;
		m_vmaAllocator = vmaAllocator; // conservé pour compat, plus utilisé pour l'alloc.

		// ---------------------------------------------------------------------
		// Garde-fou : le format RGBA16F doit supporter STORAGE_IMAGE (écriture
		// compute). Sinon échec propre (-> repli useIBL=0 côté Engine).
		// ---------------------------------------------------------------------
		VkFormatProperties fmtProps{};
		vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &fmtProps);
		if ((fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
		{
			LOG_WARN(Render, "IrradiancePass::Init: RGBA16F sans STORAGE_IMAGE_BIT, IBL diffus désactivé");
			return false;
		}

		// ---------------------------------------------------------------------
		// Cubemap image (6 layers, 1 mip) — Vulkan brut + DEVICE_LOCAL
		// ---------------------------------------------------------------------
		VkImageCreateInfo imgInfo{};
		imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		imgInfo.imageType = VK_IMAGE_TYPE_2D;
		imgInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		imgInfo.extent.width = faceSize;
		imgInfo.extent.height = faceSize;
		imgInfo.extent.depth = 1;
		imgInfo.mipLevels = 1;
		imgInfo.arrayLayers = 6;
		imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imgInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkResult r = vkCreateImage(device, &imgInfo, nullptr, &m_image);
		LOG_INFO(Render, "[IRRAD] vkCreateImage r={} img={}", (int)r, (void*)m_image);
		if (r != VK_SUCCESS || m_image == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "IrradiancePass: vkCreateImage failed");
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
			LOG_ERROR(Render, "IrradiancePass: no suitable DEVICE_LOCAL memory type");
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIdx;

		VkDeviceMemory memory = VK_NULL_HANDLE;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS || memory == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "IrradiancePass: vkAllocateMemory failed");
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		if (vkBindImageMemory(device, m_image, memory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "IrradiancePass: vkBindImageMemory failed");
			vkFreeMemory(device, memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		// On stocke VkDeviceMemory dans m_allocation (void*).
		m_allocation = reinterpret_cast<void*>(memory);

		// ---------------------------------------------------------------------
		// Vue cubemap complète (échantillonnage) + 6 vues 2D par face (storage)
		// ---------------------------------------------------------------------
		VkImageViewCreateInfo cubeViewInfo{};
		cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		cubeViewInfo.image = m_image;
		cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		cubeViewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		cubeViewInfo.subresourceRange.baseMipLevel = 0;
		cubeViewInfo.subresourceRange.levelCount = 1;
		cubeViewInfo.subresourceRange.baseArrayLayer = 0;
		cubeViewInfo.subresourceRange.layerCount = 6;
		if (vkCreateImageView(device, &cubeViewInfo, nullptr, &m_cubeView) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "IrradiancePass: vkCreateImageView (cube) failed");
			Destroy(device);
			return false;
		}

		m_faceViews.reserve(6u);
		for (uint32_t face = 0; face < 6u; ++face)
		{
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = face;
			viewInfo.subresourceRange.layerCount = 1;
			VkImageView view = VK_NULL_HANDLE;
			if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "IrradiancePass: vkCreateImageView (face) failed");
				Destroy(device);
				return false;
			}
			m_faceViews.push_back(view);
		}

		// ---------------------------------------------------------------------
		// Sampler (linéaire, sans mip — 1 seul niveau)
		// ---------------------------------------------------------------------
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
			LOG_ERROR(Render, "IrradiancePass: vkCreateSampler failed");
			Destroy(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// Descriptor set layout : binding 0 = samplerCube source, 1 = storage image
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
			LOG_ERROR(Render, "IrradiancePass: vkCreateDescriptorSetLayout failed");
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
			LOG_ERROR(Render, "IrradiancePass: vkCreateDescriptorPool failed");
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
			LOG_ERROR(Render, "IrradiancePass: vkAllocateDescriptorSets failed");
			Destroy(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// Push constants + pipeline layout
		// ---------------------------------------------------------------------
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(IrradiancePushConstants);
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &m_setLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "IrradiancePass: vkCreatePipelineLayout failed");
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
			LOG_ERROR(Render, "IrradiancePass: vkCreateShaderModule failed");
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
			LOG_ERROR(Render, "IrradiancePass: vkCreateComputePipelines failed");
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
			LOG_ERROR(Render, "IrradiancePass: vkCreateCommandPool failed");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "IrradiancePass: initialised (size={})", m_size);
		return true;
	}

	bool IrradiancePass::Generate(VkDevice device, VkQueue queue,
		VkImageView sourceCubemapView, VkSampler sourceCubemapSampler)
	{
		LOG_DEBUG(Render, "[IRRAD] Generate enter");
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
		IrradiancePushConstants pc{};
		pc.faceSize = m_size;
		pc.pad0 = 0.0f;
		pc.pad1 = 0.0f;

		for (uint32_t face = 0; face < 6u; ++face)
		{
			pc.face = face;

			// Update descriptor : cubemap source + vue face de sortie.
			VkDescriptorImageInfo srcImgInfo{};
			srcImgInfo.sampler = sourceCubemapSampler;
			srcImgInfo.imageView = sourceCubemapView;
			srcImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkDescriptorImageInfo outImgInfo{};
			outImgInfo.imageView = m_faceViews[face];
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

			// Transition de cette face en GENERAL pour l'écriture.
			VkImageMemoryBarrier toGeneral{};
			toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toGeneral.srcAccessMask = 0;
			toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			toGeneral.image = m_image;
			toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			toGeneral.subresourceRange.baseMipLevel = 0;
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
				sizeof(IrradiancePushConstants), &pc);

			const uint32_t groups = (m_size + groupSize - 1u) / groupSize;
			vkCmdDispatch(cmd, groups, groups, 1u);

			// Barrière : la prochaine face ou la transition finale voit l'écriture.
			VkImageMemoryBarrier afterWrite{};
			afterWrite.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			afterWrite.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			afterWrite.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			afterWrite.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			afterWrite.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			afterWrite.image = m_image;
			afterWrite.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			afterWrite.subresourceRange.baseMipLevel = 0;
			afterWrite.subresourceRange.levelCount = 1;
			afterWrite.subresourceRange.baseArrayLayer = face;
			afterWrite.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &afterWrite);
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

		LOG_INFO(Render, "IrradiancePass: irradiance cubemap generated");
		return true;
	}

	void IrradiancePass::Destroy(VkDevice device)
	{
		LOG_DEBUG(Render, "[IRRAD] Destroy enter");
		if (device == VK_NULL_HANDLE)
			return;
		for (VkImageView v : m_faceViews)
		{
			if (v != VK_NULL_HANDLE)
				vkDestroyImageView(device, v, nullptr);
		}
		m_faceViews.clear();
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
		if (m_image != VK_NULL_HANDLE && m_allocation != nullptr)
		{
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_allocation);
			vkDestroyImage(device, m_image, nullptr);
			vkFreeMemory(device, mem, nullptr);
			m_image = VK_NULL_HANDLE;
			m_allocation = nullptr;
		}
		m_vmaAllocator = nullptr;
		m_size = 0;
		LOG_INFO(Render, "[IRRAD] Destroy OK");
	}
}
