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
		std::fprintf(stderr, "[STAGING] Init enter device=%p vma=%p budget=%zu\n",
			(void*)device, vmaAllocator, budgetBytesPerFrame); std::fflush(stderr);

		if (device == VK_NULL_HANDLE || vmaAllocator == nullptr || budgetBytesPerFrame == 0)
		{
			std::fprintf(stderr, "[STAGING] Init: invalid params\n"); std::fflush(stderr);
			return false;
		}

		std::fprintf(stderr, "[STAGING] avant Destroy\n"); std::fflush(stderr);
		Destroy(device);
		std::fprintf(stderr, "[STAGING] Destroy OK\n"); std::fflush(stderr);

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
			std::fprintf(stderr, "[STAGING] slot %u: create buffer size=%zu\n",
				i, budgetBytesPerFrame); std::fflush(stderr);

			VkBufferCreateInfo bufInfo{};
			bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size        = static_cast<VkDeviceSize>(budgetBytesPerFrame);
			bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkBuffer buffer = VK_NULL_HANDLE;
			VkResult rBuf   = vkCreateBuffer(device, &bufInfo, nullptr, &buffer);
			std::fprintf(stderr, "[STAGING] slot %u: vkCreateBuffer r=%d buf=%p\n",
				i, (int)rBuf, (void*)buffer); std::fflush(stderr);
			if (rBuf != VK_SUCCESS || buffer == VK_NULL_HANDLE)
			{
				std::fprintf(stderr, "[STAGING] slot %u: vkCreateBuffer FAILED\n", i); std::fflush(stderr);
				Destroy(device);
				return false;
			}

			VkMemoryRequirements memReq{};
			vkGetBufferMemoryRequirements(device, buffer, &memReq);
			std::fprintf(stderr, "[STAGING] slot %u: memReq size=%llu align=%llu bits=0x%x\n",
				i,
				(unsigned long long)memReq.size,
				(unsigned long long)memReq.alignment,
				memReq.memoryTypeBits); std::fflush(stderr);

			const VkMemoryPropertyFlags flags =
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			uint32_t memTypeIdx = findMemoryType(memReq.memoryTypeBits, flags);
			std::fprintf(stderr, "[STAGING] slot %u: memTypeIdx=%u\n", i, memTypeIdx); std::fflush(stderr);
			if (memTypeIdx == UINT32_MAX)
			{
				std::fprintf(stderr, "[STAGING] slot %u: no suitable HOST_VISIBLE|COHERENT memory type\n",
					i); std::fflush(stderr);
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
			std::fprintf(stderr, "[STAGING] slot %u: vkAllocateMemory r=%d mem=%p\n",
				i, (int)rMem, (void*)memory); std::fflush(stderr);
			if (rMem != VK_SUCCESS || memory == VK_NULL_HANDLE)
			{
				std::fprintf(stderr, "[STAGING] slot %u: vkAllocateMemory FAILED\n", i); std::fflush(stderr);
				vkDestroyBuffer(device, buffer, nullptr);
				Destroy(device);
				return false;
			}

			VkResult rBind = vkBindBufferMemory(device, buffer, memory, 0);
			std::fprintf(stderr, "[STAGING] slot %u: vkBindBufferMemory r=%d\n",
				i, (int)rBind); std::fflush(stderr);
			if (rBind != VK_SUCCESS)
			{
				std::fprintf(stderr, "[STAGING] slot %u: vkBindBufferMemory FAILED\n", i); std::fflush(stderr);
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
		std::fprintf(stderr, "[STAGING] Init OK (raw Vulkan buffers)\n"); std::fflush(stderr);
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
				std::fprintf(stderr, "[STAGING] Destroy slot %u buffer=%p mem=%p\n",
					i, (void*)m_ring[i].buffer, (void*)mem); std::fflush(stderr);
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