#include "engine/render/GpuDrivenCullingPass.h"
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

	bool GpuDrivenCullingPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		const uint32_t* computeSpirv, size_t computeWordCount,
		uint32_t framesInFlight, uint32_t maxDrawItems)
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

		VkDescriptorSetLayoutBinding bindings[4]{};
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

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 4;
		layoutInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: descriptor set layout creation failed");
			Destroy(device);
			return false;
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSize.descriptorCount = m_framesInFlight * 4u;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = m_framesInFlight;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
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

		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			vkDestroyShaderModule(device, shaderModule, nullptr);
			LOG_ERROR(Render, "[GpuDrivenCullingPass] Init FAILED: compute pipeline creation failed");
			Destroy(device);
			return false;
		}
		vkDestroyShaderModule(device, shaderModule, nullptr);

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

			VkWriteDescriptorSet writes[4]{};
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

			vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
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

	void GpuDrivenCullingPass::Record(VkCommandBuffer cmd, const float* viewProjMatrix4x4, uint32_t frameIndex)
	{
		if (!IsValid() || cmd == VK_NULL_HANDLE || !viewProjMatrix4x4 || !m_slots)
			return;

		FrameSlot& slot = m_slots[frameIndex % m_framesInFlight];

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
		LOG_INFO(Render, "[GpuDrivenCullingPass] Destroyed");
	}
} // namespace engine::render
