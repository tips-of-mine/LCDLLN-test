#pragma once

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>

namespace engine::render
{
	/// Per-frame resources for 2 frames in flight: command pool, command buffer, semaphores, fence.
	struct FrameResources
	{
		VkCommandPool cmdPool = VK_NULL_HANDLE;
		VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
		VkSemaphore imageAvailable = VK_NULL_HANDLE;
		VkSemaphore renderFinished = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
	};

	/// Creates FrameResources[2]: command pools, command buffers, imageAvailable/renderFinished semaphores, fences.
	/// \param device Logical device (must be valid).
	/// \param graphicsQueueFamilyIndex Queue family for the command pools.
	/// \param out Array of 2 FrameResources to fill.
	/// \return true on success.
	bool CreateFrameResources(::VkDevice device, uint32_t graphicsQueueFamilyIndex,
		std::array<FrameResources, 2>& out);

	/// Destroys all resources in the array. Safe to call multiple times.
	void DestroyFrameResources(::VkDevice device, std::array<FrameResources, 2>& resources);
}
