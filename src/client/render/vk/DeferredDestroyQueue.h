#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <queue>

namespace engine::render
{
	/// Deferred GPU resource destruction: queue (resource + fence value), collect when fence signaled (M10.3).
	/// Use frame index as fence value; call Collect(device, completedFrameIndex) after vkWaitForFences for that frame.
	class DeferredDestroyQueue
	{
	public:
		DeferredDestroyQueue() = default;

		/// Queues a buffer for destruction after the given frame's fence is signaled.
		void PushBuffer(VkBuffer buffer, uint64_t fenceValue);
		/// Queues an image for destruction after the given frame's fence is signaled.
		void PushImage(VkImage image, uint64_t fenceValue);

		/// Frees all queued resources whose fenceValue <= completedFrameIndex. Call after vkWaitForFences(completedFrameIndex's frame).
		void Collect(VkDevice device, uint64_t completedFrameIndex);

	private:
		struct Entry
		{
			uint64_t fenceValue = 0;
			enum class Type { Buffer, Image } type = Type::Buffer;
			VkBuffer buffer = VK_NULL_HANDLE;
			VkImage image = VK_NULL_HANDLE;
		};
		std::queue<Entry> m_queue;
	};
}
