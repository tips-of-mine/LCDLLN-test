/**
 * @file VkShadowMap.cpp
 * @brief Cascaded shadow maps: 4x D32 depth images, render pass, framebuffers.
 *
 * Ticket: M04.2 — Shadow pass: depth-only render per cascade.
 */

#include "engine/render/vk/VkShadowMap.h"
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

bool CreateDepthImage(VkPhysicalDevice physicalDevice, VkDevice device,
                      uint32_t size,
                      VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView) {
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_D32_SFLOAT;
    ici.extent        = { size, size, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &outImage) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, outImage, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, outMemory, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo ivci{};
    ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image    = outImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format   = VK_FORMAT_D32_SFLOAT;
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    ivci.subresourceRange.baseMipLevel   = 0;
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(device, &ivci, nullptr, &outView) != VK_SUCCESS) {
        vkDestroyImage(device, outImage, nullptr);
        vkFreeMemory(device, outMemory, nullptr);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

} // namespace

VkShadowMap::~VkShadowMap() {
    Shutdown();
}

void VkShadowMap::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < kShadowMapCascadeCount; ++i) {
        if (m_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, m_framebuffers[i], nullptr);
            m_framebuffers[i] = VK_NULL_HANDLE;
        }
        if (m_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_views[i], nullptr);
            m_views[i] = VK_NULL_HANDLE;
        }
        if (m_images[i] != VK_NULL_HANDLE) {
            vkDestroyImage(m_device, m_images[i], nullptr);
            m_images[i] = VK_NULL_HANDLE;
        }
        if (m_memories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_memories[i], nullptr);
            m_memories[i] = VK_NULL_HANDLE;
        }
    }

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

bool VkShadowMap::Init(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t size) {
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_size           = size;

    if (size == 0) {
        return true;
    }

    for (uint32_t i = 0; i < kShadowMapCascadeCount; ++i) {
        if (!CreateDepthImage(physicalDevice, device, size,
                              m_images[i], m_memories[i], m_views[i])) {
            LOG_ERROR(Render, "VkShadowMap: failed to create cascade {} depth image", i);
            DestroyResources();
            return false;
        }
    }

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples       = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount   = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &depthAttachment;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;

    if (vkCreateRenderPass(device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkShadowMap: vkCreateRenderPass failed");
        DestroyResources();
        return false;
    }

    for (uint32_t i = 0; i < kShadowMapCascadeCount; ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = m_renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &m_views[i];
        fbci.width           = size;
        fbci.height          = size;
        fbci.layers          = 1;

        if (vkCreateFramebuffer(device, &fbci, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkShadowMap: vkCreateFramebuffer failed for cascade {}", i);
            DestroyResources();
            return false;
        }
    }

    return true;
}

bool VkShadowMap::Recreate(uint32_t size) {
    DestroyResources();
    m_size = size;
    if (size == 0) return true;
    return Init(m_physicalDevice, m_device, size);
}

void VkShadowMap::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_size           = 0;
}

VkImage VkShadowMap::GetImage(uint32_t cascadeIndex) const {
    if (cascadeIndex >= kShadowMapCascadeCount) return VK_NULL_HANDLE;
    return m_images[cascadeIndex];
}

VkImageView VkShadowMap::GetView(uint32_t cascadeIndex) const {
    if (cascadeIndex >= kShadowMapCascadeCount) return VK_NULL_HANDLE;
    return m_views[cascadeIndex];
}

VkFramebuffer VkShadowMap::GetFramebuffer(uint32_t cascadeIndex) const {
    if (cascadeIndex >= kShadowMapCascadeCount) return VK_NULL_HANDLE;
    return m_framebuffers[cascadeIndex];
}

} // namespace engine::render::vk
