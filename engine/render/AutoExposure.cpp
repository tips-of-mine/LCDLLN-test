#include "engine/render/AutoExposure.h"
#include "engine/core/Log.h"

#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
#include <cmath>
#include <cstring>

namespace engine::render
{
	bool AutoExposure::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		const uint32_t* compSpirv, size_t compWordCount)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || vmaAllocator == nullptr || !compSpirv || compWordCount == 0)
		{
			LOG_ERROR(Render, "AutoExposure::Init: invalid parameters");
			return false;
		}

		m_vmaAllocator = vmaAllocator;
		VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator);
		const VkDeviceSize luminanceBytes = kLuminanceSampleCount * sizeof(float);

		VmaAllocationCreateInfo devAllocInfo{};
		devAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		VmaAllocationCreateInfo hostAllocInfo{};
		hostAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		// ---------------------------------------------------------------------
		// Luminance buffer (device local, compute write, transfer src)
		// ---------------------------------------------------------------------
		{
			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = luminanceBytes;
			bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			VmaAllocation lumAlloc = VK_NULL_HANDLE;
			if (vmaCreateBuffer(alloc, &bufInfo, &devAllocInfo, &m_luminanceBuffer, &lumAlloc, nullptr) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vmaCreateBuffer (luminance) failed");
				return false;
			}
			m_luminanceAlloc = lumAlloc;
		}

		// ---------------------------------------------------------------------
		// Staging buffer (host visible, transfer dst, for readback)
		// ---------------------------------------------------------------------
		{
			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = luminanceBytes;
			bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			VmaAllocation stgAlloc = VK_NULL_HANDLE;
			if (vmaCreateBuffer(alloc, &bufInfo, &hostAllocInfo, &m_stagingBuffer, &stgAlloc, nullptr) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vmaCreateBuffer (staging) failed");
				Destroy(device);
				return false;
			}
			m_stagingAlloc = stgAlloc;
		}

		// ---------------------------------------------------------------------
		// Exposure buffer (host visible, persistent, 1 float)
		// ---------------------------------------------------------------------
		{
			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = sizeof(float);
			bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			VmaAllocation expAlloc = VK_NULL_HANDLE;
			if (vmaCreateBuffer(alloc, &bufInfo, &hostAllocInfo, &m_exposureBuffer, &expAlloc, nullptr) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vmaCreateBuffer (exposure) failed");
				Destroy(device);
				return false;
			}
			m_exposureAlloc = expAlloc;
			void* ptr = nullptr;
			if (vmaMapMemory(alloc, expAlloc, &ptr) == VK_SUCCESS)
			{
				m_exposure = 1.0f;
				std::memcpy(ptr, &m_exposure, sizeof(float));
				vmaUnmapMemory(alloc, expAlloc);
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
		// Descriptor set layout: image sampler + storage buffer
		// ---------------------------------------------------------------------
		{
			VkDescriptorSetLayoutBinding bindings[2]{};
			bindings[0].binding = 0;
			bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[0].descriptorCount = 1;
			bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			bindings[1].binding = 1;
			bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[1].descriptorCount = 1;
			bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 2;
			layoutInfo.pBindings = bindings;

			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vkCreateDescriptorSetLayout failed");
				Destroy(device);
				return false;
			}
		}

		{
			VkDescriptorPoolSize poolSizes[2]{};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 1;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			poolSizes[1].descriptorCount = 1;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 1;

			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vkCreateDescriptorPool failed");
				Destroy(device);
				return false;
			}
		}

		{
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = m_descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &m_descriptorSetLayout;

			if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vkAllocateDescriptorSets failed");
				Destroy(device);
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// Pipeline layout (push constant: gridSize uint)
		// ---------------------------------------------------------------------
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushRange.offset = 0;
			pushRange.size = sizeof(uint32_t);

			VkPipelineLayoutCreateInfo plInfo{};
			plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plInfo.setLayoutCount = 1;
			plInfo.pSetLayouts = &m_descriptorSetLayout;
			plInfo.pushConstantRangeCount = 1;
			plInfo.pPushConstantRanges = &pushRange;

			if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vkCreatePipelineLayout failed");
				Destroy(device);
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// Compute pipeline
		// ---------------------------------------------------------------------
		{
			VkShaderModuleCreateInfo smInfo{};
			smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			smInfo.codeSize = compWordCount * sizeof(uint32_t);
			smInfo.pCode = compSpirv;

			VkShaderModule compModule = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &smInfo, nullptr, &compModule) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "AutoExposure: vkCreateShaderModule failed");
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
				LOG_ERROR(Render, "AutoExposure: vkCreateComputePipelines failed");
				vkDestroyShaderModule(device, compModule, nullptr);
				Destroy(device);
				return false;
			}
			vkDestroyShaderModule(device, compModule, nullptr);
		}

		// Descriptor set is updated each frame in Record with current image view and luminance buffer.
		return true;
	}

	void AutoExposure::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		ResourceId idSceneColorHDR,
		VkExtent2D /*extent*/)
	{
		if (!IsValid()) return;

		VkImageView hdrView = registry.getImageView(idSceneColorHDR);
		if (hdrView == VK_NULL_HANDLE) return;

		VkDescriptorImageInfo imgInfo{};
		imgInfo.sampler = m_sampler;
		imgInfo.imageView = hdrView;
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorBufferInfo bufInfo{};
		bufInfo.buffer = m_luminanceBuffer;
		bufInfo.offset = 0;
		bufInfo.range = kLuminanceSampleCount * sizeof(float);

		VkWriteDescriptorSet writes[2]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_descriptorSet;
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &imgInfo;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_descriptorSet;
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].pBufferInfo = &bufInfo;

		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

		uint32_t gridSize = kLuminanceGridSize;
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &gridSize);

		vkCmdDispatch(cmd, kLuminanceGridSize / 8, kLuminanceGridSize / 8, 1);

		// Barrier: compute write -> transfer read
		VkMemoryBarrier memBarrier{};
		memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 1, &memBarrier, 0, nullptr, 0, nullptr);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size = kLuminanceSampleCount * sizeof(float);
		vkCmdCopyBuffer(cmd, m_luminanceBuffer, m_stagingBuffer, 1, &copyRegion);
	}

	void AutoExposure::Update(VkDevice device, float dt, float key, float speed)
	{
		(void)device;
		if (m_stagingAlloc == nullptr || m_vmaAllocator == nullptr) return;
		VmaAllocator alloc = static_cast<VmaAllocator>(m_vmaAllocator);

		void* ptr = nullptr;
		if (vmaMapMemory(alloc, static_cast<VmaAllocation>(m_stagingAlloc), &ptr) != VK_SUCCESS)
			return;

		const float* samples = static_cast<const float*>(ptr);
		double sum = 0.0;
		for (uint32_t i = 0; i < kLuminanceSampleCount; ++i)
			sum += static_cast<double>(samples[i]);
		vmaUnmapMemory(alloc, static_cast<VmaAllocation>(m_stagingAlloc));

		float avgLog = static_cast<float>(sum / static_cast<double>(kLuminanceSampleCount));
		float logAvgLuminance = std::exp(avgLog);
		const float kEpsilon = 1e-6f;
		float targetExposure = key / (logAvgLuminance + kEpsilon);
		targetExposure = std::max(0.001f, std::min(10.0f, targetExposure));

		// exposure = lerp(prev, target, 1 - exp(-dt*speed))
		float blend = 1.0f - std::exp(-dt * speed);
		m_exposure = m_exposure + (targetExposure - m_exposure) * blend;
		m_exposure = std::max(0.001f, std::min(10.0f, m_exposure));

		// Write to persistent exposure buffer
		if (m_exposureAlloc != nullptr && m_vmaAllocator != nullptr)
		{
			VmaAllocator a = static_cast<VmaAllocator>(m_vmaAllocator);
			if (vmaMapMemory(a, static_cast<VmaAllocation>(m_exposureAlloc), &ptr) == VK_SUCCESS)
			{
				std::memcpy(ptr, &m_exposure, sizeof(float));
				vmaUnmapMemory(a, static_cast<VmaAllocation>(m_exposureAlloc));
			}
		}
	}

	void AutoExposure::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;

		if (m_pipeline != VK_NULL_HANDLE)
			{ vkDestroyPipeline(device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
		if (m_pipelineLayout != VK_NULL_HANDLE)
			{ vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
		if (m_descriptorPool != VK_NULL_HANDLE)
			{ vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
		if (m_descriptorSetLayout != VK_NULL_HANDLE)
			{ vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
		if (m_sampler != VK_NULL_HANDLE)
			{ vkDestroySampler(device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }

		if (m_vmaAllocator != nullptr)
		{
			VmaAllocator alloc = static_cast<VmaAllocator>(m_vmaAllocator);
			if (m_luminanceBuffer != VK_NULL_HANDLE && m_luminanceAlloc != nullptr)
				{ vmaDestroyBuffer(alloc, m_luminanceBuffer, static_cast<VmaAllocation>(m_luminanceAlloc)); m_luminanceBuffer = VK_NULL_HANDLE; m_luminanceAlloc = nullptr; }
			if (m_stagingBuffer != VK_NULL_HANDLE && m_stagingAlloc != nullptr)
				{ vmaDestroyBuffer(alloc, m_stagingBuffer, static_cast<VmaAllocation>(m_stagingAlloc)); m_stagingBuffer = VK_NULL_HANDLE; m_stagingAlloc = nullptr; }
			if (m_exposureBuffer != VK_NULL_HANDLE && m_exposureAlloc != nullptr)
				{ vmaDestroyBuffer(alloc, m_exposureBuffer, static_cast<VmaAllocation>(m_exposureAlloc)); m_exposureBuffer = VK_NULL_HANDLE; m_exposureAlloc = nullptr; }
		}
		m_vmaAllocator = nullptr;
	}
}
