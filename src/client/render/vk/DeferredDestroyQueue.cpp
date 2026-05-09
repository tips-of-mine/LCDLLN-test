#include "engine/render/vk/DeferredDestroyQueue.h"

namespace engine::render
{
	void DeferredDestroyQueue::PushBuffer(VkBuffer buffer, uint64_t fenceValue)
	{
		if (buffer == VK_NULL_HANDLE)
			return;
		Entry e;
		e.fenceValue = fenceValue;
		e.type = Entry::Type::Buffer;
		e.buffer = buffer;
		e.image = VK_NULL_HANDLE;
		m_queue.push(e);
	}

	void DeferredDestroyQueue::PushImage(VkImage image, uint64_t fenceValue)
	{
		if (image == VK_NULL_HANDLE)
			return;
		Entry e;
		e.fenceValue = fenceValue;
		e.type = Entry::Type::Image;
		e.buffer = VK_NULL_HANDLE;
		e.image = image;
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
				if (e.type == Entry::Type::Buffer && e.buffer != VK_NULL_HANDLE)
					vkDestroyBuffer(device, e.buffer, nullptr);
				else if (e.type == Entry::Type::Image && e.image != VK_NULL_HANDLE)
					vkDestroyImage(device, e.image, nullptr);
			}
			else
				remaining.push(e);
		}
		m_queue = std::move(remaining);
	}
}
