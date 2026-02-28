/**
 * @file VkTaaHistory.cpp
 * @brief TAA history ping-pong images (HistoryA / HistoryB).
 */

#include "engine/render/vk/VkTaaHistory.h"
#include "engine/core/Log.h"

#include <cstring>

namespace engine::render::vk {

namespace {

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice,
                        uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace

VkTaaHistory::~VkTaaHistory() {
    Shutdown();
}

void VkTaaHistory::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;
    for (uint32_t i = 0; i < 2u; ++i) {
        if (m_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_views[i], nullptr);
            m_views[i] = VK_NULL_HANDLE;
        }
        if (m_images[i] != VK_NULL_HANDLE) {
            vkDestroyImage(m_device, m_images[i], nullptr);
            m_images[i] = VK_NULL_HANDLE;
        }
        if (m_memory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_memory[i], nullptr);
            m_memory[i] = VK_NULL_HANDLE;
        }
    }
}

bool VkTaaHistory::Init(VkPhysicalDevice physicalDevice,
                        VkDevice         device,
                        uint32_t         width,
                        uint32_t         height,
                        VkFormat         format) {
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_extent         = {width, height};
    m_format         = format;

    if (width == 0 || height == 0) {
        return true;
    }

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = {width, height, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    for (uint32_t i = 0; i < 2u; ++i) {
        if (vkCreateImage(device, &ici, nullptr, &m_images[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkTaaHistory: vkCreateImage failed for index {}", i);
            DestroyResources();
            return false;
        }

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, m_images[i], &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice,
                                                   memReqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR(Render, "VkTaaHistory: no suitable memory type");
            DestroyResources();
            return false;
        }

        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkTaaHistory: vkAllocateMemory failed for index {}", i);
            DestroyResources();
            return false;
        }

        if (vkBindImageMemory(device, m_images[i], m_memory[i], 0) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkTaaHistory: vkBindImageMemory failed for index {}", i);
            DestroyResources();
            return false;
        }

        VkImageViewCreateInfo ivci{};
        ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image    = m_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format   = format;
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel   = 0;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &ivci, nullptr, &m_views[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkTaaHistory: vkCreateImageView failed for index {}", i);
            DestroyResources();
            return false;
        }
    }

    return true;
}

bool VkTaaHistory::Recreate(uint32_t width, uint32_t height) {
    DestroyResources();
    m_extent = {width, height};
    if (width == 0 || height == 0) {
        return true;
    }
    return Init(m_physicalDevice, m_device, width, height, m_format);
}

void VkTaaHistory::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_format         = VK_FORMAT_UNDEFINED;
    m_extent         = {0, 0};
}

VkImage VkTaaHistory::GetImage(uint32_t index) const noexcept {
    return (index < 2u) ? m_images[index] : VK_NULL_HANDLE;
}

VkImageView VkTaaHistory::GetView(uint32_t index) const noexcept {
    return (index < 2u) ? m_views[index] : VK_NULL_HANDLE;
}

} // namespace engine::render::vk
