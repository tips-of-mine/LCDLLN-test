/**
 * @file VkSsaoBlur.cpp
 * @brief SSAO_Blur temp and output (R16F) for bilateral blur H/V passes.
 *
 * Ticket: M06.3 — SSAO: bilateral blur (2 passes).
 */

#include "engine/render/vk/VkSsaoBlur.h"
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

bool CreateR16FImage(VkPhysicalDevice physicalDevice, VkDevice device,
                    uint32_t width, uint32_t height,
                    VkImage* outImage, VkDeviceMemory* outMemory, VkImageView* outView) {
    const VkFormat format = VK_FORMAT_R16_SFLOAT;
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
    if (vkCreateImage(device, &ici, nullptr, outImage) != VK_SUCCESS)
        return false;
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, *outImage, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocInfo, nullptr, outMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, *outImage, *outMemory, 0) != VK_SUCCESS) {
        vkDestroyImage(device, *outImage, nullptr);
        *outImage = VK_NULL_HANDLE;
        return false;
    }
    VkImageViewCreateInfo ivci{};
    ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image   = *outImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format   = format;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &ivci, nullptr, outView) != VK_SUCCESS) {
        vkDestroyImage(device, *outImage, nullptr);
        vkFreeMemory(device, *outMemory, nullptr);
        *outImage = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

} // namespace

VkSsaoBlur::~VkSsaoBlur() {
    Shutdown();
}

void VkSsaoBlur::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_framebufferTemp != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_framebufferTemp, nullptr);
        m_framebufferTemp = VK_NULL_HANDLE;
    }
    if (m_framebufferOutput != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_framebufferOutput, nullptr);
        m_framebufferOutput = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    if (m_viewTemp != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_viewTemp, nullptr);
        m_viewTemp = VK_NULL_HANDLE;
    }
    if (m_imageTemp != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_imageTemp, nullptr);
        m_imageTemp = VK_NULL_HANDLE;
    }
    if (m_memoryTemp != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memoryTemp, nullptr);
        m_memoryTemp = VK_NULL_HANDLE;
    }
    if (m_viewOutput != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_viewOutput, nullptr);
        m_viewOutput = VK_NULL_HANDLE;
    }
    if (m_imageOutput != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_imageOutput, nullptr);
        m_imageOutput = VK_NULL_HANDLE;
    }
    if (m_memoryOutput != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memoryOutput, nullptr);
        m_memoryOutput = VK_NULL_HANDLE;
    }
}

bool VkSsaoBlur::Init(VkPhysicalDevice physicalDevice,
                      VkDevice         device,
                      uint32_t         width,
                      uint32_t         height) {
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_extent         = {width, height};

    if (width == 0 || height == 0) {
        return true;
    }

    if (!CreateR16FImage(physicalDevice, device, width, height,
                         &m_imageTemp, &m_memoryTemp, &m_viewTemp)) {
        LOG_ERROR(Render, "VkSsaoBlur: temp image creation failed");
        DestroyResources();
        return false;
    }
    if (!CreateR16FImage(physicalDevice, device, width, height,
                         &m_imageOutput, &m_memoryOutput, &m_viewOutput)) {
        LOG_ERROR(Render, "VkSsaoBlur: output image creation failed");
        DestroyResources();
        return false;
    }

    const VkFormat format = VK_FORMAT_R16_SFLOAT;
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = format;
    colorAttachment.samples       = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
        LOG_ERROR(Render, "VkSsaoBlur: vkCreateRenderPass failed");
        DestroyResources();
        return false;
    }

    VkFramebufferCreateInfo fbci{};
    fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass      = m_renderPass;
    fbci.attachmentCount = 1;
    fbci.width           = width;
    fbci.height          = height;
    fbci.layers          = 1;
    fbci.pAttachments    = &m_viewTemp;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &m_framebufferTemp) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSsaoBlur: framebuffer temp failed");
        DestroyResources();
        return false;
    }
    fbci.pAttachments = &m_viewOutput;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &m_framebufferOutput) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSsaoBlur: framebuffer output failed");
        DestroyResources();
        return false;
    }
    return true;
}

bool VkSsaoBlur::Recreate(uint32_t width, uint32_t height) {
    DestroyResources();
    m_extent = {width, height};
    if (width == 0 || height == 0) {
        return true;
    }
    return Init(m_physicalDevice, m_device, width, height);
}

void VkSsaoBlur::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_extent         = {0, 0};
}

} // namespace engine::render::vk
