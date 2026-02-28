/**
 * @file VkGBuffer.cpp
 * @brief GBuffer images (A/B/C + Depth), render pass and framebuffer.
 */

#include "engine/render/vk/VkGBuffer.h"
#include "engine/core/Log.h"

#include <array>
#include <cstdint>

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

bool CreateColorImage(VkPhysicalDevice physicalDevice, VkDevice device,
                      uint32_t width, uint32_t height, VkFormat format,
                      VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView) {
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = { width, height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &outImage) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, outImage, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex  = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
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
    ivci.format   = format;
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
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

bool CreateDepthImage(VkPhysicalDevice physicalDevice, VkDevice device,
                      uint32_t width, uint32_t height, VkFormat format,
                      VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView) {
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = { width, height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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
    ivci.format   = format;
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    ivci.subresourceRange.baseMipLevel   = 0;
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.baseArrayLayer  = 0;
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

VkGBuffer::~VkGBuffer() {
    Shutdown();
}

void VkGBuffer::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    auto destroyImage = [this](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (img != VK_NULL_HANDLE) {
            vkDestroyImage(m_device, img, nullptr);
            img = VK_NULL_HANDLE;
        }
        if (mem != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, mem, nullptr);
            mem = VK_NULL_HANDLE;
        }
    };
    destroyImage(m_imageA, m_memoryA, m_viewA);
    destroyImage(m_imageB, m_memoryB, m_viewB);
    destroyImage(m_imageC, m_memoryC, m_viewC);
    destroyImage(m_imageDepth, m_memoryDepth, m_viewDepth);
}

bool VkGBuffer::Init(VkPhysicalDevice physicalDevice, VkDevice device,
                     uint32_t width, uint32_t height) {
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_extent         = { width, height };

    if (width == 0 || height == 0)
        return true;

    if (!CreateColorImage(physicalDevice, device, width, height,
                         VK_FORMAT_R8G8B8A8_SRGB,
                         m_imageA, m_memoryA, m_viewA)) {
        LOG_ERROR(Render, "VkGBuffer: failed to create image A");
        DestroyResources();
        return false;
    }
    if (!CreateColorImage(physicalDevice, device, width, height,
                         VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                         m_imageB, m_memoryB, m_viewB)) {
        LOG_ERROR(Render, "VkGBuffer: failed to create image B");
        DestroyResources();
        return false;
    }
    if (!CreateColorImage(physicalDevice, device, width, height,
                         VK_FORMAT_R8G8B8A8_UNORM,
                         m_imageC, m_memoryC, m_viewC)) {
        LOG_ERROR(Render, "VkGBuffer: failed to create image C");
        DestroyResources();
        return false;
    }
    if (!CreateDepthImage(physicalDevice, device, width, height,
                         VK_FORMAT_D32_SFLOAT,
                         m_imageDepth, m_memoryDepth, m_viewDepth)) {
        LOG_ERROR(Render, "VkGBuffer: failed to create depth image");
        DestroyResources();
        return false;
    }

    std::array<VkAttachmentDescription, 4> attachments{};
    attachments[0].format         = VK_FORMAT_R8G8B8A8_SRGB;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format         = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    attachments[1].samples       = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[2].format         = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp  = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[3].format         = VK_FORMAT_D32_SFLOAT;
    attachments[3].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 3> colorRefs{};
    colorRefs[0].attachment = 0;
    colorRefs[0].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorRefs[1].attachment = 1;
    colorRefs[1].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorRefs[2].attachment = 2;
    colorRefs[2].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 3;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount   = 3;
    subpass.pColorAttachments      = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 4;
    rpci.pAttachments    = attachments.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;

    if (vkCreateRenderPass(device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkGBuffer: vkCreateRenderPass failed");
        DestroyResources();
        return false;
    }

    std::array<VkImageView, 4> fbAttachments = { m_viewA, m_viewB, m_viewC, m_viewDepth };
    VkFramebufferCreateInfo fbci{};
    fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass      = m_renderPass;
    fbci.attachmentCount = 4;
    fbci.pAttachments    = fbAttachments.data();
    fbci.width           = width;
    fbci.height          = height;
    fbci.layers          = 1;

    if (vkCreateFramebuffer(device, &fbci, nullptr, &m_framebuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkGBuffer: vkCreateFramebuffer failed");
        DestroyResources();
        return false;
    }

    return true;
}

bool VkGBuffer::Recreate(uint32_t width, uint32_t height) {
    DestroyResources();
    m_extent = { width, height };
    if (width == 0 || height == 0)
        return true;
    return Init(m_physicalDevice, m_device, width, height);
}

void VkGBuffer::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_extent         = { 0, 0 };
}

} // namespace engine::render::vk
