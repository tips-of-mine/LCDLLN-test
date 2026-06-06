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

		/// Queues a buffer (et sa mémoire associée) pour destruction après que le fence
		/// de la frame donnée est signalé. La mémoire est libérée APRÈS le buffer.
		/// \param memory mémoire à libérer (VK_NULL_HANDLE si gérée ailleurs).
		void PushBuffer(VkBuffer buffer, VkDeviceMemory memory, uint64_t fenceValue);
		/// Queues une image (sa vue + sa mémoire) pour destruction après que le fence
		/// de la frame donnée est signalé. Ordre : vue, image, puis mémoire.
		/// \param view vue d'image à détruire (VK_NULL_HANDLE si aucune).
		/// \param memory mémoire à libérer (VK_NULL_HANDLE si gérée ailleurs).
		void PushImage(VkImage image, VkImageView view, VkDeviceMemory memory, uint64_t fenceValue);

		/// Frees all queued resources whose fenceValue <= completedFrameIndex. Call after vkWaitForFences(completedFrameIndex's frame).
		void Collect(VkDevice device, uint64_t completedFrameIndex);

	private:
		struct Entry
		{
			uint64_t fenceValue = 0;
			enum class Type { Buffer, Image } type = Type::Buffer;
			VkBuffer buffer = VK_NULL_HANDLE;
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
		};
		std::queue<Entry> m_queue;
	};
}
