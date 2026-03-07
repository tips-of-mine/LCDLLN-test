#include "engine/render/vk/VkUtils.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace engine::render::vk
{
	uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
	{
		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1u << i)) != 0
				&& (memProps.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}
		return UINT32_MAX;
	}
}
