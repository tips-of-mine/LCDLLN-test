/**
 * @file VkBloomPyramid.cpp
 * @brief Bloom mip pyramid (1/2..1/32).
 */

#include "engine/render/vk/VkBloomPyramid.h"
#include "engine/core/Log.h"

#include <algorithm>

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

constexpr VkFormat kBloomFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

bool CreateMipLevel(VkPhysicalDevice physicalDevice, VkDevice device,
                    uint32_t width, uint32_t height,
                    VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView,
                    VkRenderPass& outRenderPass, VkFramebuffer& outFramebuffer) {
    if (width == 0 || height == 0) return true;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = kBloomFormat;
    ici.extent        = {width, height, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    ivci.format   = kBloomFormat;
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

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = kBloomFormat;
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
    if (vkCreateRenderPass(device, &rpci, nullptr, &outRenderPass) != VK_SUCCESS) {
        vkDestroyImageView(device, outView, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        vkFreeMemory(device, outMemory, nullptr);
        outView = VK_NULL_HANDLE;
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return false;
    }

    VkFramebufferCreateInfo fbci{};
    fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass      = outRenderPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments    = &outView;
    fbci.width           = width;
    fbci.height          = height;
    fbci.layers          = 1;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &outFramebuffer) != VK_SUCCESS) {
        vkDestroyRenderPass(device, outRenderPass, nullptr);
        vkDestroyImageView(device, outView, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        vkFreeMemory(device, outMemory, nullptr);
        outRenderPass = VK_NULL_HANDLE;
        outView = VK_NULL_HANDLE;
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void DestroyMipLevel(VkDevice device,
                     VkImage& img, VkDeviceMemory& mem, VkImageView& view,
                     VkRenderPass& rp, VkFramebuffer& fb) {
    if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, fb, nullptr); fb = VK_NULL_HANDLE; }
    if (rp != VK_NULL_HANDLE) { vkDestroyRenderPass(device, rp, nullptr); rp = VK_NULL_HANDLE; }
    if (view != VK_NULL_HANDLE) { vkDestroyImageView(device, view, nullptr); view = VK_NULL_HANDLE; }
    if (img != VK_NULL_HANDLE) { vkDestroyImage(device, img, nullptr); img = VK_NULL_HANDLE; }
    if (mem != VK_NULL_HANDLE) { vkFreeMemory(device, mem, nullptr); mem = VK_NULL_HANDLE; }
}

} // namespace

VkBloomPyramid::~VkBloomPyramid() {
    Shutdown();
}

void VkBloomPyramid::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;
    for (uint32_t i = 0; i < kBloomMipCount; ++i) {
        DestroyMipLevel(m_device, m_images[i], m_memory[i], m_views[i],
                       m_renderPasses[i], m_framebuffers[i]);
        m_extents[i] = {0, 0};
    }
}

bool VkBloomPyramid::Init(VkPhysicalDevice physicalDevice,
                          VkDevice         device,
                          uint32_t         fullWidth,
                          uint32_t         fullHeight) {
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_fullWidth      = fullWidth;
    m_fullHeight     = fullHeight;

    if (fullWidth == 0 || fullHeight == 0) return true;

    for (uint32_t i = 0; i < kBloomMipCount; ++i) {
        const uint32_t div = 1u << (i + 1u); // 2, 4, 8, 16, 32
        const uint32_t w = std::max(1u, fullWidth / div);
        const uint32_t h = std::max(1u, fullHeight / div);
        m_extents[i] = {w, h};
        if (!CreateMipLevel(physicalDevice, device, w, h,
                           m_images[i], m_memory[i], m_views[i],
                           m_renderPasses[i], m_framebuffers[i])) {
            LOG_ERROR(Render, "VkBloomPyramid: failed to create mip level {}", i);
            DestroyResources();
            return false;
        }
    }
    return true;
}

bool VkBloomPyramid::Recreate(uint32_t fullWidth, uint32_t fullHeight) {
    DestroyResources();
    m_fullWidth  = fullWidth;
    m_fullHeight = fullHeight;
    if (fullWidth == 0 || fullHeight == 0) return true;
    return Init(m_physicalDevice, m_device, fullWidth, fullHeight);
}

void VkBloomPyramid::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_fullWidth      = 0;
    m_fullHeight     = 0;
}

VkExtent2D VkBloomPyramid::GetExtent(uint32_t level) const noexcept {
    return (level < kBloomMipCount) ? m_extents[level] : VkExtent2D{0, 0};
}

VkImage VkBloomPyramid::GetImage(uint32_t level) const noexcept {
    return (level < kBloomMipCount) ? m_images[level] : VK_NULL_HANDLE;
}

VkImageView VkBloomPyramid::GetView(uint32_t level) const noexcept {
    return (level < kBloomMipCount) ? m_views[level] : VK_NULL_HANDLE;
}

VkRenderPass VkBloomPyramid::GetRenderPass(uint32_t level) const noexcept {
    return (level < kBloomMipCount) ? m_renderPasses[level] : VK_NULL_HANDLE;
}

VkFramebuffer VkBloomPyramid::GetFramebuffer(uint32_t level) const noexcept {
    return (level < kBloomMipCount) ? m_framebuffers[level] : VK_NULL_HANDLE;
}

} // namespace engine::render::vk
