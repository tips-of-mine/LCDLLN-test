#include "engine/render/vk/StagingAllocator.h"
#include "engine/core/Log.h"

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
		if (device == VK_NULL_HANDLE || vmaAllocator == nullptr || budgetBytesPerFrame == 0)
		{
			LOG_ERROR(Render, "StagingAllocator::Init: invalid parameters");
			return false;
		}
		Destroy(device);
		m_device = device;
		m_vmaAllocator = vmaAllocator;
		VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator);

		VmaAllocationCreateInfo hostAllocInfo{};
		hostAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		for (uint32_t i = 0; i < kRingSize; ++i)
		{
			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = static_cast<VkDeviceSize>(budgetBytesPerFrame);
			bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

			VmaAllocation vmaAlloc = VK_NULL_HANDLE;
			VkResult result = vmaCreateBuffer(alloc, &bufInfo, &hostAllocInfo,
				&m_ring[i].buffer, &vmaAlloc, nullptr);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "StagingAllocator: vmaCreateBuffer failed for slot {}", i);
				Destroy(device);
				return false;
			}
			m_ring[i].vmaAllocation = vmaAlloc;
			m_ring[i].sizeBytes = static_cast<VkDeviceSize>(budgetBytesPerFrame);
		}
		m_currentSlot = 0;
		m_currentOffset = 0;
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
