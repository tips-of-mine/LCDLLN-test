#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace engine::render::vk
{
	/// Returns a memory type index that satisfies the given type filter and property flags.
	/// \param physicalDevice  Vulkan physical device.
	/// \param typeFilter      Bitmask of allowed memory type indices (from VkMemoryRequirements::memoryTypeBits).
	/// \param properties      Required memory property flags (e.g. DEVICE_LOCAL, HOST_VISIBLE | HOST_COHERENT).
	/// \return Memory type index, or UINT32_MAX if no suitable type exists.
	uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
}
