/**
 * @file VkSceneColorHDR.cpp
 * @brief Offscreen SceneColor_HDR (R16G16B16A16_SFLOAT) for lighting output.
 */

#include "engine/render/vk/VkSceneColorHDR.h"
#include "engine/core/Log.h"

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

VkSceneColorHDR::~VkSceneColorHDR() {
    Shutdown();
}

void VkSceneColorHDR::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

bool VkSceneColorHDR::Init(VkPhysicalDevice physicalDevice,
                           VkDevice         device,
                           uint32_t         width,
                           uint32_t         height) {
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_extent         = {width, height};

    if (width == 0 || height == 0) {
        return true;
    }

    const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = {width, height, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &m_image) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSceneColorHDR: vkCreateImage failed");
        DestroyResources();
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, m_image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        LOG_ERROR(Render, "VkSceneColorHDR: no suitable memory type");
        DestroyResources();
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSceneColorHDR: vkAllocateMemory failed");
        DestroyResources();
        return false;
    }
    if (vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSceneColorHDR: vkBindImageMemory failed");
        DestroyResources();
        return false;
    }

    VkImageViewCreateInfo ivci{};
    ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image    = m_image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format   = format;
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.baseMipLevel   = 0;
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(device, &ivci, nullptr, &m_imageView) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSceneColorHDR: vkCreateImageView failed");
        DestroyResources();
        return false;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = format;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &colorAttachment;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    if (vkCreateRenderPass(device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSceneColorHDR: vkCreateRenderPass failed");
        DestroyResources();
        return false;
    }

    VkFramebufferCreateInfo fbci{};
    fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass      = m_renderPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments    = &m_imageView;
    fbci.width           = width;
    fbci.height         = height;
    fbci.layers         = 1;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &m_framebuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSceneColorHDR: vkCreateFramebuffer failed");
        DestroyResources();
        return false;
    }
    return true;
}

bool VkSceneColorHDR::Recreate(uint32_t width, uint32_t height) {
    DestroyResources();
    m_extent = {width, height};
    if (width == 0 || height == 0) {
        return true;
    }
    return Init(m_physicalDevice, m_device, width, height);
}

void VkSceneColorHDR::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_extent         = {0, 0};
}

} // namespace engine::render::vk
