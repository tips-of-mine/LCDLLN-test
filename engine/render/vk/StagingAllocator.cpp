#include "engine/render/vk/StagingAllocator.h"
#include "engine/core/Log.h"

#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
#include <cstddef>

namespace engine::render
{
	namespace
	{
		constexpr VkDeviceSize kAlignment = 4; // minimal for transfer
	}

	bool StagingAllocator::Init(VkDevice device, void* vmaAllocator, size_t budgetBytesPerFrame)
	{
	    std::fprintf(stderr, "[STAGING] Init enter device=%p vma=%p budget=%zu\n", (void*)device, vmaAllocator, budgetBytesPerFrame); std::fflush(stderr);
	
	    if (device == VK_NULL_HANDLE || vmaAllocator == nullptr || budgetBytesPerFrame == 0)
	    {
	        std::fprintf(stderr, "[STAGING] Init: invalid params\n"); std::fflush(stderr);
	        return false;  // LOG_ERROR supprimé temporairement
	    }
	
	    std::fprintf(stderr, "[STAGING] avant Destroy\n"); std::fflush(stderr);
	    Destroy(device);
	    std::fprintf(stderr, "[STAGING] Destroy OK\n"); std::fflush(stderr);
	
	    m_device = device;
	    m_vmaAllocator = vmaAllocator;
	    VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator);
	
	    VmaAllocationCreateInfo hostAllocInfo{};
	    // Ancienne API VMA 2.x — déprécié et instable en VMA 3.x :
		// hostAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		// Nouvelle API VMA 3.x :
		hostAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		hostAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		
	    for (uint32_t i = 0; i < kRingSize; ++i)
	    {
	        std::fprintf(stderr, "[STAGING] avant vmaCreateBuffer slot %u\n", i); std::fflush(stderr);
	        VkBufferCreateInfo bufInfo{};
			bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size        = static_cast<VkDeviceSize>(budgetBytesPerFrame);
			bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	
	        VmaAllocation vmaAlloc = VK_NULL_HANDLE;
	        VkResult result = vmaCreateBuffer(alloc, &bufInfo, &hostAllocInfo,
	            &m_ring[i].buffer, &vmaAlloc, nullptr);
	        std::fprintf(stderr, "[STAGING] vmaCreateBuffer slot %u result=%d\n", i, (int)result); std::fflush(stderr);
	
	        if (result != VK_SUCCESS)
	        {
	            std::fprintf(stderr, "[STAGING] vmaCreateBuffer FAILED slot %u\n", i); std::fflush(stderr);
	            Destroy(device);
	            return false;
	        }
	        m_ring[i].vmaAllocation = vmaAlloc;
	        m_ring[i].sizeBytes     = static_cast<VkDeviceSize>(budgetBytesPerFrame);
	    }
	    m_currentSlot   = 0;
	    m_currentOffset = 0;
	    std::fprintf(stderr, "[STAGING] Init OK\n"); std::fflush(stderr);
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
		const VkDeviceSize alignedSize = (static_cast<VkDeviceSize>(sizeBytes) + kAlignment - 1) & ~(kAlignment - 1);
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
		m_currentSlot = frameIndex % kRingSize;
		m_currentOffset = 0;
	}

	VkBuffer StagingAllocator::GetCurrentBuffer() const
	{
		return m_ring[m_currentSlot].buffer;
	}

	void StagingAllocator::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE || m_vmaAllocator == nullptr)
			return;
		VmaAllocator alloc = static_cast<VmaAllocator>(m_vmaAllocator);
		for (uint32_t i = 0; i < kRingSize; ++i)
		{
			if (m_ring[i].buffer != VK_NULL_HANDLE && m_ring[i].vmaAllocation != nullptr)
			{
				vmaDestroyBuffer(alloc, m_ring[i].buffer, static_cast<VmaAllocation>(m_ring[i].vmaAllocation));
				m_ring[i].buffer = VK_NULL_HANDLE;
				m_ring[i].vmaAllocation = nullptr;
				m_ring[i].sizeBytes = 0;
			}
		}
		m_device = VK_NULL_HANDLE;
		m_vmaAllocator = nullptr;
		m_currentSlot = 0;
		m_currentOffset = 0;
	}
}
