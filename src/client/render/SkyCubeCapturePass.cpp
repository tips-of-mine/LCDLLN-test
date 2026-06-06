#include "src/client/render/SkyCubeCapturePass.h"
#include "src/client/render/PipelineCache.h"

#include "src/shared/core/Log.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>

namespace engine::render
{
	namespace
	{
		/// Layout push-constant miroir std430 EXACT de `sky_capture.comp`.
		/// vec3 alignés en vec4 ; total verrouillé à 96 octets par static_assert.
		struct SkyCapturePushConstants
		{
			uint32_t face;          // offset 0
			uint32_t faceSize;      // offset 4
			float    pad0;          // offset 8
			float    pad1;          // offset 12
			float    lightDir[4];   // offset 16 (vec4 : .xyz utilisé)
			float    zenithColor[4]; // offset 32
			float    horizonColor[4]; // offset 48
			float    moonDir[4];    // offset 64
			float    moonParams[4]; // offset 80 : {intensity, phase, illumination, pad}
		};
		static_assert(sizeof(SkyCapturePushConstants) == 96,
			"SkyCapturePushConstants doit faire 96 octets (miroir std430 de sky_capture.comp)");
	}

	bool SkyCubeCapturePass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		uint32_t faceSize,
		const uint32_t* compSpirv, size_t compWordCount,
		uint32_t queueFamilyIndex,
		VkPipelineCache pipelineCache)
	{
		LOG_INFO(Render, "[SKYCAP] Init enter faceSize={}", faceSize);
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE
			|| !compSpirv || compWordCount == 0 || faceSize == 0)
		{
			LOG_ERROR(Render, "SkyCubeCapturePass::Init: invalid arguments");
			return false;
		}

		m_faceSize = faceSize;
		m_vmaAllocator = vmaAllocator; // conservé pour compat, plus utilisé pour l'alloc.

		// ---------------------------------------------------------------------
		// Vérif support storage du format RGBA16F (repli propre si absent).
		// ---------------------------------------------------------------------
		{
			VkFormatProperties fmtProps{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &fmtProps);
			if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
			{
				LOG_ERROR(Render, "SkyCubeCapturePass: VK_FORMAT_R16G16B16A16_SFLOAT does not support STORAGE_IMAGE on this device — sky capture disabled");
				return false;
			}
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
		LOG_INFO(Render, "[SKYCAP] vkCreateImage r={} img={}", (int)r, (void*)m_image);
		if (r != VK_SUCCESS || m_image == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateImage failed");
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
			LOG_ERROR(Render, "SkyCubeCapturePass: no suitable DEVICE_LOCAL memory type");
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkAllocateMemory failed");
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		if (vkBindImageMemory(device, m_image, memory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SkyCubeCapturePass: vkBindImageMemory failed");
			vkFreeMemory(device, memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}

		// On stocke VkDeviceMemory dans m_allocation (void*).
		m_allocation = reinterpret_cast<void*>(memory);

		// ---------------------------------------------------------------------
		// Full cubemap view (sampling) + per-face views (storage write)
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateImageView (cube) failed");
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
				LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateImageView (face) failed");
				Destroy(device);
				return false;
			}
			m_faceViews.push_back(view);
		}

		// ---------------------------------------------------------------------
		// Sampler (linear, mipmapMode NEAREST, maxLod 0 — 1 seul mip)
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateSampler failed");
			Destroy(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// Descriptor set layout: binding 0 = storage image (output face)
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateDescriptorSetLayout failed");
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateDescriptorPool failed");
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkAllocateDescriptorSets failed");
			Destroy(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// Push constants + pipeline layout
		// ---------------------------------------------------------------------
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(SkyCapturePushConstants);
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &m_setLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreatePipelineLayout failed");
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateShaderModule failed");
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateComputePipelines failed");
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
			LOG_ERROR(Render, "SkyCubeCapturePass: vkCreateCommandPool failed");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "SkyCubeCapturePass: initialised (faceSize={})", m_faceSize);
		return true;
	}

	bool SkyCubeCapturePass::Generate(VkDevice device, VkQueue queue, const SkyCaptureParams& params)
	{
		LOG_DEBUG(Render, "[SKYCAP] Generate enter");
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

		// Construit le push-constant miroir std430 depuis les paramètres ciel.
		SkyCapturePushConstants pc{};
		pc.faceSize = m_faceSize;
		pc.pad0 = 0.0f;
		pc.pad1 = 0.0f;
		pc.lightDir[0] = params.lightDir[0];
		pc.lightDir[1] = params.lightDir[1];
		pc.lightDir[2] = params.lightDir[2];
		pc.lightDir[3] = 0.0f;
		pc.zenithColor[0] = params.zenithColor[0];
		pc.zenithColor[1] = params.zenithColor[1];
		pc.zenithColor[2] = params.zenithColor[2];
		pc.zenithColor[3] = 0.0f;
		pc.horizonColor[0] = params.horizonColor[0];
		pc.horizonColor[1] = params.horizonColor[1];
		pc.horizonColor[2] = params.horizonColor[2];
		pc.horizonColor[3] = 0.0f;
		pc.moonDir[0] = params.moonDir[0];
		pc.moonDir[1] = params.moonDir[1];
		pc.moonDir[2] = params.moonDir[2];
		pc.moonDir[3] = 0.0f;
		pc.moonParams[0] = params.moonIntensity;
		pc.moonParams[1] = params.moonPhase;
		pc.moonParams[2] = params.moonIllumination;
		pc.moonParams[3] = 0.0f;

		const uint32_t groupSize = 8u;
		const uint32_t groups = (m_faceSize + groupSize - 1u) / groupSize;

		for (uint32_t face = 0; face < 6u; ++face)
		{
			pc.face = face;

			// Update descriptor: output face view (storage).
			VkDescriptorImageInfo outImgInfo{};
			outImgInfo.imageView = m_faceViews[face];
			outImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = m_descSet;
			write.dstBinding = 0;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			write.pImageInfo = &outImgInfo;
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

			// Transition this face to GENERAL for write.
			VkImageMemoryBarrier toGeneral{};
			toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toGeneral.srcAccessMask = 0;
			toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
				sizeof(SkyCapturePushConstants), &pc);

			vkCmdDispatch(cmd, groups, groups, 1u);

			// Barrier GENERAL -> SHADER_READ_ONLY for later sampling.
			VkImageMemoryBarrier afterWrite{};
			afterWrite.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			afterWrite.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			afterWrite.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			afterWrite.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			afterWrite.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			afterWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			afterWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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

		LOG_INFO(Render, "SkyCubeCapturePass: sky cubemap captured");
		return true;
	}

	void SkyCubeCapturePass::Destroy(VkDevice device)
	{
		LOG_DEBUG(Render, "[SKYCAP] Destroy enter");
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
		m_faceSize = 0;
		LOG_INFO(Render, "[SKYCAP] Destroy OK");
	}
}
