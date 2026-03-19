#include "engine/render/vk/StagingAllocator.h"
#include "engine/core/Log.h"

#include <vk_mem_alloc.h>
#include <cstddef>

namespace engine::render
{
	namespace
	{
		// Alignement minimal pour les copies GPU (M10.4).
		constexpr VkDeviceSize kAlignment = 4;
	}

	bool StagingAllocator::Init(VkDevice device, void* vmaAllocator, size_t budgetBytesPerFrame)
	{
LOG_INFO(Render, "[STAGING] Init enter device={} vma={} budget={}", (void*)device, vmaAllocator, budgetBytesPerFrame);

		if (device == VK_NULL_HANDLE || vmaAllocator == nullptr || budgetBytesPerFrame == 0)
		{
			LOG_WARN(Render, "[STAGING] Init: invalid params");
			return false;
		}

		LOG_INFO(Render, "[STAGING] avant Destroy");
		Destroy(device);
		LOG_INFO(Render, "[STAGING] Destroy OK");

		m_device       = device;
		m_vmaAllocator = vmaAllocator; // conservé pour info, plus utilisé pour l’alloc.

		// On récupère le physical device via VMA, mais on n’utilise plus vmaCreateBuffer.
		VmaAllocator     alloc     = static_cast<VmaAllocator>(vmaAllocator);
		VmaAllocatorInfo allocInfo{};
		vmaGetAllocatorInfo(alloc, &allocInfo);
		VkPhysicalDevice physDev = allocInfo.physicalDevice;

		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

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

		for (uint32_t i = 0; i < kRingSize; ++i)
		{
LOG_DEBUG(Render, "[STAGING] slot {}: create buffer size={}", i, budgetBytesPerFrame);

			VkBufferCreateInfo bufInfo{};
			bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size        = static_cast<VkDeviceSize>(budgetBytesPerFrame);
			bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkBuffer buffer = VK_NULL_HANDLE;
			VkResult rBuf   = vkCreateBuffer(device, &bufInfo, nullptr, &buffer);
LOG_INFO(Render, "[STAGING] slot {}: vkCreateBuffer r={} buf={}", i, (int)rBuf, (void*)buffer);
			if (rBuf != VK_SUCCESS || buffer == VK_NULL_HANDLE)
			{
				LOG_WARN(Render, "[STAGING] slot {}: vkCreateBuffer FAILED", i);
				Destroy(device);
				return false;
			}

			VkMemoryRequirements memReq{};
			vkGetBufferMemoryRequirements(device, buffer, &memReq);
LOG_DEBUG(Render, "[STAGING] slot {}: memReq size={} align={} bits=0x{}", i, (unsigned long long)memReq.size, (unsigned long long)memReq.alignment, memReq.memoryTypeBits);

			const VkMemoryPropertyFlags flags =
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			uint32_t memTypeIdx = findMemoryType(memReq.memoryTypeBits, flags);
			LOG_DEBUG(Render, "[STAGING] slot {}: memTypeIdx={}", i, memTypeIdx);
			if (memTypeIdx == UINT32_MAX)
			{
LOG_DEBUG(Render, "[STAGING] slot {}: no suitable HOST_VISIBLE|COHERENT memory type", i);
				vkDestroyBuffer(device, buffer, nullptr);
				Destroy(device);
				return false;
			}

			VkMemoryAllocateInfo allocVk{};
			allocVk.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocVk.allocationSize  = memReq.size;
			allocVk.memoryTypeIndex = memTypeIdx;

			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkResult rMem = vkAllocateMemory(device, &allocVk, nullptr, &memory);
LOG_INFO(Render, "[STAGING] slot {}: vkAllocateMemory r={} mem={}", i, (int)rMem, (void*)memory);
			if (rMem != VK_SUCCESS || memory == VK_NULL_HANDLE)
			{
				LOG_WARN(Render, "[STAGING] slot {}: vkAllocateMemory FAILED", i);
				vkDestroyBuffer(device, buffer, nullptr);
				Destroy(device);
				return false;
			}

			VkResult rBind = vkBindBufferMemory(device, buffer, memory, 0);
LOG_INFO(Render, "[STAGING] slot {}: vkBindBufferMemory r={}", i, (int)rBind);
			if (rBind != VK_SUCCESS)
			{
				LOG_WARN(Render, "[STAGING] slot {}: vkBindBufferMemory FAILED", i);
				vkFreeMemory(device, memory, nullptr);
				vkDestroyBuffer(device, buffer, nullptr);
				Destroy(device);
				return false;
			}

			m_ring[i].buffer        = buffer;
			// On réutilise vmaAllocation pour stocker le VkDeviceMemory.
			m_ring[i].vmaAllocation = reinterpret_cast<void*>(memory);
			m_ring[i].sizeBytes     = static_cast<VkDeviceSize>(budgetBytesPerFrame);
		}

		m_currentSlot   = 0;
		m_currentOffset = 0;
		LOG_INFO(Render, "[STAGING] Init OK (raw Vulkan buffers)");
		return true;
	}

	VkBuffer StagingAllocator::Allocate(size_t sizeBytes, VkDeviceSize& outOffset)
	{
		if (m_device == VK_NULL_HANDLE || sizeBytes == 0)
		{
			outOffset = 0;
			return VK_NULL_HANDLE;
		}
		Slot& slot = m_ring[m_currentSlot];
		if (slot.buffer == VK_NULL_HANDLE)
		{
			outOffset = 0;
			return VK_NULL_HANDLE;
		}
		const VkDeviceSize alignedSize =
			(static_cast<VkDeviceSize>(sizeBytes) + kAlignment - 1) & ~(kAlignment - 1);
		if (m_currentOffset + alignedSize > slot.sizeBytes)
		{
			outOffset = 0;
			return VK_NULL_HANDLE;
		}
		outOffset = m_currentOffset;
		m_currentOffset += alignedSize;
		return slot.buffer;
	}

	void StagingAllocator::BeginFrame(uint32_t frameIndex)
	{
		m_currentSlot   = frameIndex % kRingSize;
		m_currentOffset = 0;
	}

	VkBuffer StagingAllocator::GetCurrentBuffer() const
	{
		return m_ring[m_currentSlot].buffer;
	}

	void StagingAllocator::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
			return;

		for (uint32_t i = 0; i < kRingSize; ++i)
		{
			if (m_ring[i].buffer != VK_NULL_HANDLE && m_ring[i].vmaAllocation != nullptr)
			{
				VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_ring[i].vmaAllocation);
LOG_DEBUG(Render, "[STAGING] Destroy slot {} buffer={} mem={}", i, (void*)m_ring[i].buffer, (void*)mem);
				vkDestroyBuffer(device, m_ring[i].buffer, nullptr);
				vkFreeMemory(device, mem, nullptr);
				m_ring[i].buffer        = VK_NULL_HANDLE;
				m_ring[i].vmaAllocation = nullptr;
				m_ring[i].sizeBytes     = 0;
			}
		}
		m_device       = VK_NULL_HANDLE;
		m_vmaAllocator = nullptr;
		m_currentSlot  = 0;
		m_currentOffset = 0;
	}
}