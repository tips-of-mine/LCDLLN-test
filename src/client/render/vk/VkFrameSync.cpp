#include "engine/render/vk/VkFrameSync.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

namespace engine::render
{
	bool CreateFrameResources(::VkDevice device, uint32_t graphicsQueueFamilyIndex,
		std::array<FrameResources, 2>& out)
	{
		if (device == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "CreateFrameResources: invalid device");
			return false;
		}

		for (size_t i = 0; i < out.size(); ++i)
		{
			FrameResources& fr = out[i];

			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;

			VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &fr.cmdPool);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "vkCreateCommandPool failed for frame {}: {}", i, static_cast<int>(result));
				DestroyFrameResources(device, out);
				return false;
			}

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = fr.cmdPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			result = vkAllocateCommandBuffers(device, &allocInfo, &fr.cmdBuffer);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "vkAllocateCommandBuffers failed for frame {}: {}", i, static_cast<int>(result));
				DestroyFrameResources(device, out);
				return false;
			}

			VkSemaphoreCreateInfo semInfo{};
			semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			result = vkCreateSemaphore(device, &semInfo, nullptr, &fr.imageAvailable);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "vkCreateSemaphore imageAvailable failed for frame {}: {}", i, static_cast<int>(result));
				DestroyFrameResources(device, out);
				return false;
			}

			result = vkCreateSemaphore(device, &semInfo, nullptr, &fr.renderFinished);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "vkCreateSemaphore renderFinished failed for frame {}: {}", i, static_cast<int>(result));
				DestroyFrameResources(device, out);
				return false;
			}

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			result = vkCreateFence(device, &fenceInfo, nullptr, &fr.fence);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "vkCreateFence failed for frame {}: {}", i, static_cast<int>(result));
				DestroyFrameResources(device, out);
				return false;
			}
		}

		LOG_INFO(Render, "FrameResources[2] created");
		return true;
	}

	void DestroyFrameResources(::VkDevice device, std::array<FrameResources, 2>& resources)
	{
		if (device == VK_NULL_HANDLE)
		{
			return;
		}

		for (FrameResources& fr : resources)
		{
			if (fr.fence != VK_NULL_HANDLE)
			{
				vkDestroyFence(device, fr.fence, nullptr);
				fr.fence = VK_NULL_HANDLE;
			}
			if (fr.renderFinished != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, fr.renderFinished, nullptr);
				fr.renderFinished = VK_NULL_HANDLE;
			}
			if (fr.imageAvailable != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, fr.imageAvailable, nullptr);
				fr.imageAvailable = VK_NULL_HANDLE;
			}
			if (fr.cmdPool != VK_NULL_HANDLE)
			{
				vkDestroyCommandPool(device, fr.cmdPool, nullptr);
				fr.cmdPool = VK_NULL_HANDLE;
			}
			fr.cmdBuffer = VK_NULL_HANDLE;
		}
		LOG_INFO(Render, "FrameResources[2] destroyed");
	}
}
