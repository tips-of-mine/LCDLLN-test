#include "src/client/render/vk/DeferredDestroyQueue.h"

namespace engine::render
{
	void DeferredDestroyQueue::PushBuffer(VkBuffer buffer, VkDeviceMemory memory, uint64_t fenceValue)
	{
		if (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE)
			return;
		Entry e;
		e.fenceValue = fenceValue;
		e.type = Entry::Type::Buffer;
		e.buffer = buffer;
		e.image = VK_NULL_HANDLE;
		e.view = VK_NULL_HANDLE;
		e.memory = memory;
		m_queue.push(e);
	}

	void DeferredDestroyQueue::PushImage(VkImage image, VkImageView view, VkDeviceMemory memory, uint64_t fenceValue)
	{
		if (image == VK_NULL_HANDLE && view == VK_NULL_HANDLE && memory == VK_NULL_HANDLE)
			return;
		Entry e;
		e.fenceValue = fenceValue;
		e.type = Entry::Type::Image;
		e.buffer = VK_NULL_HANDLE;
		e.image = image;
		e.view = view;
		e.memory = memory;
		m_queue.push(e);
	}

	void DeferredDestroyQueue::Collect(VkDevice device, uint64_t completedFrameIndex)
	{
		if (device == VK_NULL_HANDLE)
			return;
		std::queue<Entry> remaining;
		while (!m_queue.empty())
		{
			Entry e = m_queue.front();
			m_queue.pop();
			if (e.fenceValue <= completedFrameIndex)
			{
				// Ordre obligatoire : détruire la vue + le buffer/image AVANT de
				// libérer la mémoire (la mémoire ne doit plus être référencée par
				// aucune ressource vivante au moment du vkFreeMemory).
				if (e.type == Entry::Type::Buffer)
				{
					if (e.buffer != VK_NULL_HANDLE)
						vkDestroyBuffer(device, e.buffer, nullptr);
				}
				else if (e.type == Entry::Type::Image)
				{
					if (e.view != VK_NULL_HANDLE)
						vkDestroyImageView(device, e.view, nullptr);
					if (e.image != VK_NULL_HANDLE)
						vkDestroyImage(device, e.image, nullptr);
				}
				if (e.memory != VK_NULL_HANDLE)
					vkFreeMemory(device, e.memory, nullptr);
			}
			else
				remaining.push(e);
		}
		m_queue = std::move(remaining);
	}
}
