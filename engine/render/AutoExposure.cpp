#include "engine/render/AutoExposure.h"
#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::render
{
	VkShaderModule AutoExposure::CreateShaderModule(VkDevice device, const uint32_t* spirv, size_t wordCount)
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

	bool AutoExposure::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		const uint32_t* histogramCompSpirv, size_t histogramCompWordCount,
		const uint32_t* histogramAvgCompSpirv, size_t histogramAvgCompWordCount,
		float histogramPercentileLow,
		float histogramPercentileHigh,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
			!histogramCompSpirv || histogramCompWordCount == 0 ||
			!histogramAvgCompSpirv || histogramAvgCompWordCount == 0)
		{
			LOG_ERROR(Render, "[AutoExposure] Init FAILED: invalid parameters");
			return false;
		}

		m_vmaAllocator = vmaAllocator; // conservé pour compat, non utilisé pour l'alloc actuelle.
		m_histogramParams.percentileLow  = std::max(0.0f, std::min(histogramPercentileLow, 1.0f));
		m_histogramParams.percentileHigh = std::max(m_histogramParams.percentileLow, std::min(histogramPercentileHigh, 1.0f));
		const VkDeviceSize histogramBytes = kHistogramBinCount * sizeof(uint32_t);
		const VkDeviceSize stagingBytes = sizeof(float);

		if (m_histogramParams.percentileHigh <= m_histogramParams.percentileLow)
		{
			LOG_ERROR(Render, "[AutoExposure] Init FAILED: invalid histogram percentiles low={} high={}",
				m_histogramParams.percentileLow, m_histogramParams.percentileHigh);
			return false;
		}

		// Helpers mémoire (comme BrdfLutPass / SpecularPrefilter / StagingAllocator)
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

		auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
			VkMemoryPropertyFlags memFlags,
			VkBuffer& outBuffer, void*& outAlloc) -> bool
		{
			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = size;
			bufInfo.usage = usage;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (vkCreateBuffer(device, &bufInfo, nullptr, &outBuffer) != VK_SUCCESS || outBuffer == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "AutoExposure: vkCreateBuffer failed (usage=0x{:x})", usage);
				return false;
			}

			VkMemoryRequirements memReq{};
			vkGetBufferMemoryRequirements(device, outBuffer, &memReq);
			uint32_t memTypeIdx = findMemoryType(memReq.memoryTypeBits, memFlags);
			if (memTypeIdx == UINT32_MAX)
			{
				LOG_ERROR(Render, "AutoExposure: no suitable memory type (flags=0x{:x})", memFlags);
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = memTypeIdx;

			VkDeviceMemory mem = VK_NULL_HANDLE;
			if (vkAllocateMemory(device, &allocInfo, nullptr, &mem) != VK_SUCCESS || mem == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "AutoExposure: vkAllocateMemory failed");
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			if (vkBindBufferMemory(device, outBuffer, mem, 0) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vkBindBufferMemory failed");
				vkFreeMemory(device, mem, nullptr);
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			outAlloc = reinterpret_cast<void*>(mem);
			return true;
		};

		for (uint32_t slot = 0; slot < kAESlots; ++slot)
		{
			// -----------------------------------------------------------------
			// Histogram buffer (device local, compute read/write, transfer dst for reset)
			// -----------------------------------------------------------------
			if (!createBuffer(histogramBytes,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					m_histogramBuffer[slot], m_histogramAlloc[slot]))
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: histogram buffer slot {} creation failed", slot);
				Destroy(device);
				return false;
			}

			// -----------------------------------------------------------------
			// Staging buffer (host visible, compute write, for readback)
			// -----------------------------------------------------------------
			if (!createBuffer(stagingBytes,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					m_stagingBuffer[slot], m_stagingAlloc[slot]))
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: staging buffer slot {} creation failed", slot);
				Destroy(device);
				return false;
			}

			LOG_INFO(Render, "[AutoExposure] Slot {} buffers created (histogramBins={})", slot, kHistogramBinCount);
		}

		// ---------------------------------------------------------------------
		// Exposure buffer (host visible, persistent, 1 float)
		// ---------------------------------------------------------------------
		if (!createBuffer(sizeof(float),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				m_exposureBuffer, m_exposureAlloc))
		{
			Destroy(device);
			return false;
		}
		// init exposure à 1.0 dans le buffer
		{
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_exposureAlloc);
			void* ptr = nullptr;
			if (vkMapMemory(device, mem, 0, sizeof(float), 0, &ptr) == VK_SUCCESS)
			{
				m_exposure = 1.0f;
				std::memcpy(ptr, &m_exposure, sizeof(float));
				vkUnmapMemory(device, mem);
			}
		}

		// ---------------------------------------------------------------------
		// Sampler for HDR image
		// ---------------------------------------------------------------------
		{
			VkSamplerCreateInfo si{};
			si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter = VK_FILTER_LINEAR;
			si.minFilter = VK_FILTER_LINEAR;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.maxLod = 0.0f;

			if (vkCreateSampler(device, &si, nullptr, &m_sampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vkCreateSampler failed");
				Destroy(device);
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// Descriptor set layouts:
		// - histogram pass: image sampler + histogram buffer
		// - average pass: histogram buffer + staging output
		// ---------------------------------------------------------------------
		{
			VkDescriptorSetLayoutBinding histogramBindings[2]{};
			histogramBindings[0].binding = 0;
			histogramBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			histogramBindings[0].descriptorCount = 1;
			histogramBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			histogramBindings[1].binding = 1;
			histogramBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			histogramBindings[1].descriptorCount = 1;
			histogramBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			VkDescriptorSetLayoutCreateInfo histogramLayoutInfo{};
			histogramLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			histogramLayoutInfo.bindingCount = 2;
			histogramLayoutInfo.pBindings = histogramBindings;

			if (vkCreateDescriptorSetLayout(device, &histogramLayoutInfo, nullptr, &m_histogramDescriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: histogram descriptor set layout creation failed");
				Destroy(device);
				return false;
			}

			VkDescriptorSetLayoutBinding averageBindings[2]{};
			averageBindings[0].binding = 0;
			averageBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			averageBindings[0].descriptorCount = 1;
			averageBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			averageBindings[1].binding = 1;
			averageBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			averageBindings[1].descriptorCount = 1;
			averageBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			VkDescriptorSetLayoutCreateInfo averageLayoutInfo{};
			averageLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			averageLayoutInfo.bindingCount = 2;
			averageLayoutInfo.pBindings = averageBindings;

			if (vkCreateDescriptorSetLayout(device, &averageLayoutInfo, nullptr, &m_averageDescriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: average descriptor set layout creation failed");
				Destroy(device);
				return false;
			}
		}

		{
			VkDescriptorPoolSize poolSizes[2]{};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = kAESlots;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			poolSizes[1].descriptorCount = kAESlots * 3u;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = kAESlots * 2u;

			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: descriptor pool creation failed");
				Destroy(device);
				return false;
			}
		}

		{
			VkDescriptorSetLayout layouts[kAESlots]{};
			for (uint32_t slot = 0; slot < kAESlots; ++slot)
				layouts[slot] = m_histogramDescriptorSetLayout;

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = m_descriptorPool;
			allocInfo.descriptorSetCount = kAESlots;
			allocInfo.pSetLayouts = layouts;

			if (vkAllocateDescriptorSets(device, &allocInfo, m_histogramDescriptorSets) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: histogram descriptor set allocation failed");
				Destroy(device);
				return false;
			}
		}

		{
			VkDescriptorSetLayout layouts[kAESlots]{};
			for (uint32_t slot = 0; slot < kAESlots; ++slot)
				layouts[slot] = m_averageDescriptorSetLayout;

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = m_descriptorPool;
			allocInfo.descriptorSetCount = kAESlots;
			allocInfo.pSetLayouts = layouts;

			if (vkAllocateDescriptorSets(device, &allocInfo, m_averageDescriptorSets) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: average descriptor set allocation failed");
				Destroy(device);
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// Pipeline layouts
		// ---------------------------------------------------------------------
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushRange.offset = 0;
			pushRange.size = sizeof(HistogramPushConstants);

			VkPipelineLayoutCreateInfo histogramPipelineLayoutInfo{};
			histogramPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			histogramPipelineLayoutInfo.setLayoutCount = 1;
			histogramPipelineLayoutInfo.pSetLayouts = &m_histogramDescriptorSetLayout;
			histogramPipelineLayoutInfo.pushConstantRangeCount = 1;
			histogramPipelineLayoutInfo.pPushConstantRanges = &pushRange;

			if (vkCreatePipelineLayout(device, &histogramPipelineLayoutInfo, nullptr, &m_histogramPipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: histogram pipeline layout creation failed");
				Destroy(device);
				return false;
			}

			VkPipelineLayoutCreateInfo averagePipelineLayoutInfo{};
			averagePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			averagePipelineLayoutInfo.setLayoutCount = 1;
			averagePipelineLayoutInfo.pSetLayouts = &m_averageDescriptorSetLayout;
			averagePipelineLayoutInfo.pushConstantRangeCount = 1;
			averagePipelineLayoutInfo.pPushConstantRanges = &pushRange;

			if (vkCreatePipelineLayout(device, &averagePipelineLayoutInfo, nullptr, &m_averagePipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: average pipeline layout creation failed");
				Destroy(device);
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// Compute pipelines
		// ---------------------------------------------------------------------
		{
			VkShaderModule histogramModule = CreateShaderModule(device, histogramCompSpirv, histogramCompWordCount);
			if (histogramModule == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: histogram shader module creation failed");
				Destroy(device);
				return false;
			}

			VkShaderModule averageModule = CreateShaderModule(device, histogramAvgCompSpirv, histogramAvgCompWordCount);
			if (averageModule == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: average shader module creation failed");
				vkDestroyShaderModule(device, histogramModule, nullptr);
				Destroy(device);
				return false;
			}

			VkPipelineShaderStageCreateInfo histogramStageInfo{};
			histogramStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			histogramStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			histogramStageInfo.module = histogramModule;
			histogramStageInfo.pName = "main";

			VkComputePipelineCreateInfo histogramPipelineInfo{};
			histogramPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			histogramPipelineInfo.stage = histogramStageInfo;
			histogramPipelineInfo.layout = m_histogramPipelineLayout;

			AssertPipelineCreationAllowed();
			PipelineCache::RegisterWarmupKey(HashComputePsoKey(m_histogramPipelineLayout, histogramCompWordCount));
			if (vkCreateComputePipelines(device, pipelineCache, 1, &histogramPipelineInfo, nullptr, &m_histogramPipeline) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: histogram compute pipeline creation failed");
				vkDestroyShaderModule(device, averageModule, nullptr);
				vkDestroyShaderModule(device, histogramModule, nullptr);
				Destroy(device);
				return false;
			}

			VkPipelineShaderStageCreateInfo averageStageInfo{};
			averageStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			averageStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			averageStageInfo.module = averageModule;
			averageStageInfo.pName = "main";

			VkComputePipelineCreateInfo averagePipelineInfo{};
			averagePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			averagePipelineInfo.stage = averageStageInfo;
			averagePipelineInfo.layout = m_averagePipelineLayout;

			PipelineCache::RegisterWarmupKey(HashComputePsoKey(m_averagePipelineLayout, histogramAvgCompWordCount));
			if (vkCreateComputePipelines(device, pipelineCache, 1, &averagePipelineInfo, nullptr, &m_averagePipeline) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[AutoExposure] Init FAILED: average compute pipeline creation failed");
				vkDestroyShaderModule(device, averageModule, nullptr);
				vkDestroyShaderModule(device, histogramModule, nullptr);
				Destroy(device);
				return false;
			}

			vkDestroyShaderModule(device, averageModule, nullptr);
			vkDestroyShaderModule(device, histogramModule, nullptr);
		}

		LOG_INFO(Render, "[AutoExposure] Init OK (slots={}, percentileLow={}, percentileHigh={})",
			kAESlots, m_histogramParams.percentileLow, m_histogramParams.percentileHigh);
		return true;
	}

	void AutoExposure::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		ResourceId idSceneColorHDR,
		VkExtent2D extent,
		uint32_t frameIndex)
	{
		if (!IsValid()) return;
		const uint32_t slot = frameIndex % kAESlots;

		VkImageView hdrView = registry.getImageView(idSceneColorHDR);
		if (hdrView == VK_NULL_HANDLE) return;

		VkDescriptorImageInfo imgInfo{};
		imgInfo.sampler = m_sampler;
		imgInfo.imageView = hdrView;
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorBufferInfo histogramBufferInfo{};
		histogramBufferInfo.buffer = m_histogramBuffer[slot];
		histogramBufferInfo.offset = 0;
		histogramBufferInfo.range = kHistogramBinCount * sizeof(uint32_t);

		VkDescriptorBufferInfo stagingBufferInfo{};
		stagingBufferInfo.buffer = m_stagingBuffer[slot];
		stagingBufferInfo.offset = 0;
		stagingBufferInfo.range = sizeof(float);

		VkWriteDescriptorSet histogramWrites[2]{};
		histogramWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		histogramWrites[0].dstSet = m_histogramDescriptorSets[slot];
		histogramWrites[0].dstBinding = 0;
		histogramWrites[0].dstArrayElement = 0;
		histogramWrites[0].descriptorCount = 1;
		histogramWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		histogramWrites[0].pImageInfo = &imgInfo;
		histogramWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		histogramWrites[1].dstSet = m_histogramDescriptorSets[slot];
		histogramWrites[1].dstBinding = 1;
		histogramWrites[1].dstArrayElement = 0;
		histogramWrites[1].descriptorCount = 1;
		histogramWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		histogramWrites[1].pBufferInfo = &histogramBufferInfo;

		VkWriteDescriptorSet averageWrites[2]{};
		averageWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		averageWrites[0].dstSet = m_averageDescriptorSets[slot];
		averageWrites[0].dstBinding = 0;
		averageWrites[0].dstArrayElement = 0;
		averageWrites[0].descriptorCount = 1;
		averageWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		averageWrites[0].pBufferInfo = &histogramBufferInfo;
		averageWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		averageWrites[1].dstSet = m_averageDescriptorSets[slot];
		averageWrites[1].dstBinding = 1;
		averageWrites[1].dstArrayElement = 0;
		averageWrites[1].descriptorCount = 1;
		averageWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		averageWrites[1].pBufferInfo = &stagingBufferInfo;

		VkWriteDescriptorSet writes[4]{ histogramWrites[0], histogramWrites[1], averageWrites[0], averageWrites[1] };
		vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);

		vkCmdFillBuffer(cmd, m_histogramBuffer[slot], 0, histogramBufferInfo.range, 0u);

		VkBufferMemoryBarrier resetBarrier{};
		resetBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		resetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		resetBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		resetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		resetBarrier.buffer = m_histogramBuffer[slot];
		resetBarrier.offset = 0;
		resetBarrier.size = histogramBufferInfo.range;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 1, &resetBarrier, 0, nullptr);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histogramPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histogramPipelineLayout, 0, 1, &m_histogramDescriptorSets[slot], 0, nullptr);
		vkCmdPushConstants(cmd, m_histogramPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HistogramPushConstants), &m_histogramParams);

		const uint32_t reducedWidth = std::max(1u, (extent.width + 3u) / 4u);
		const uint32_t reducedHeight = std::max(1u, (extent.height + 3u) / 4u);
		vkCmdDispatch(cmd, (reducedWidth + 15u) / 16u, (reducedHeight + 15u) / 16u, 1u);

		VkBufferMemoryBarrier histogramBarrier{};
		histogramBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		histogramBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		histogramBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		histogramBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		histogramBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		histogramBarrier.buffer = m_histogramBuffer[slot];
		histogramBarrier.offset = 0;
		histogramBarrier.size = histogramBufferInfo.range;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 1, &histogramBarrier, 0, nullptr);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_averagePipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_averagePipelineLayout, 0, 1, &m_averageDescriptorSets[slot], 0, nullptr);
		vkCmdPushConstants(cmd, m_averagePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HistogramPushConstants), &m_histogramParams);
		vkCmdDispatch(cmd, 1u, 1u, 1u);

		VkBufferMemoryBarrier stagingBarrier{};
		stagingBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		stagingBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		stagingBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		stagingBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		stagingBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		stagingBarrier.buffer = m_stagingBuffer[slot];
		stagingBarrier.offset = 0;
		stagingBarrier.size = stagingBufferInfo.range;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT,
			0, 0, nullptr, 1, &stagingBarrier, 0, nullptr);
	}

	void AutoExposure::Update(VkDevice device, float dt, float key, float speed, uint32_t frameIndex)
	{
		const uint32_t slot = (frameIndex + 1u) % kAESlots;
		if (device == VK_NULL_HANDLE || m_stagingAlloc[slot] == nullptr)
			return;

		VkDeviceMemory stgMem = reinterpret_cast<VkDeviceMemory>(m_stagingAlloc[slot]);
		void* ptr = nullptr;
		if (vkMapMemory(device, stgMem, 0, VK_WHOLE_SIZE, 0, &ptr) != VK_SUCCESS)
			return;

		const float avgLog = *static_cast<const float*>(ptr);
		vkUnmapMemory(device, stgMem);

		float avgLuminance = std::exp2(avgLog);
		const float kEpsilon = 1e-6f;
		float targetExposure = key / (avgLuminance + kEpsilon);
		targetExposure = std::max(0.001f, std::min(10.0f, targetExposure));

		// exposure = lerp(prev, target, 1 - exp(-dt*speed))
		float blend = 1.0f - std::exp(-dt * speed);
		m_exposure = m_exposure + (targetExposure - m_exposure) * blend;
		m_exposure = std::max(0.001f, std::min(10.0f, m_exposure));

		// Write to persistent exposure buffer
		if (m_exposureAlloc != nullptr)
		{
			VkDeviceMemory expMem = reinterpret_cast<VkDeviceMemory>(m_exposureAlloc);
			if (vkMapMemory(device, expMem, 0, sizeof(float), 0, &ptr) == VK_SUCCESS)
			{
				std::memcpy(ptr, &m_exposure, sizeof(float));
				vkUnmapMemory(device, expMem);
			}
		}
	}

	void AutoExposure::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;

		if (m_averagePipeline != VK_NULL_HANDLE)
			{ vkDestroyPipeline(device, m_averagePipeline, nullptr); m_averagePipeline = VK_NULL_HANDLE; }
		if (m_histogramPipeline != VK_NULL_HANDLE)
			{ vkDestroyPipeline(device, m_histogramPipeline, nullptr); m_histogramPipeline = VK_NULL_HANDLE; }
		if (m_averagePipelineLayout != VK_NULL_HANDLE)
			{ vkDestroyPipelineLayout(device, m_averagePipelineLayout, nullptr); m_averagePipelineLayout = VK_NULL_HANDLE; }
		if (m_histogramPipelineLayout != VK_NULL_HANDLE)
			{ vkDestroyPipelineLayout(device, m_histogramPipelineLayout, nullptr); m_histogramPipelineLayout = VK_NULL_HANDLE; }
		if (m_descriptorPool != VK_NULL_HANDLE)
			{ vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
		if (m_averageDescriptorSetLayout != VK_NULL_HANDLE)
			{ vkDestroyDescriptorSetLayout(device, m_averageDescriptorSetLayout, nullptr); m_averageDescriptorSetLayout = VK_NULL_HANDLE; }
		if (m_histogramDescriptorSetLayout != VK_NULL_HANDLE)
			{ vkDestroyDescriptorSetLayout(device, m_histogramDescriptorSetLayout, nullptr); m_histogramDescriptorSetLayout = VK_NULL_HANDLE; }
		if (m_sampler != VK_NULL_HANDLE)
			{ vkDestroySampler(device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }

		for (uint32_t slot = 0; slot < kAESlots; ++slot)
		{
			if (m_histogramBuffer[slot] != VK_NULL_HANDLE && m_histogramAlloc[slot] != nullptr)
			{
				VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_histogramAlloc[slot]);
				vkDestroyBuffer(device, m_histogramBuffer[slot], nullptr);
				vkFreeMemory(device, mem, nullptr);
				m_histogramBuffer[slot] = VK_NULL_HANDLE;
				m_histogramAlloc[slot] = nullptr;
			}
			if (m_stagingBuffer[slot] != VK_NULL_HANDLE && m_stagingAlloc[slot] != nullptr)
			{
				VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_stagingAlloc[slot]);
				vkDestroyBuffer(device, m_stagingBuffer[slot], nullptr);
				vkFreeMemory(device, mem, nullptr);
				m_stagingBuffer[slot] = VK_NULL_HANDLE;
				m_stagingAlloc[slot] = nullptr;
			}
		}
		if (m_exposureBuffer != VK_NULL_HANDLE && m_exposureAlloc != nullptr)
		{
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_exposureAlloc);
			vkDestroyBuffer(device, m_exposureBuffer, nullptr);
			vkFreeMemory(device, mem, nullptr);
			m_exposureBuffer = VK_NULL_HANDLE;
			m_exposureAlloc = nullptr;
		}
		m_vmaAllocator = nullptr;
		LOG_INFO(Render, "[AutoExposure] Destroyed");
	}
}
