#include "engine/render/vk/VkSwapchain.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

using namespace engine::core;

namespace engine::render::vk {

namespace {

/**
 * @brief Chooses surface format: prefer SRGB if available.
 */
VkSurfaceFormatKHR ChooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) {

    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats[0];
}

/**
 * @brief Chooses present mode: MAILBOX if available, else FIFO.
 */
VkPresentModeKHR ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) {

    for (VkPresentModeKHR m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            return m;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

/**
 * @brief Clamps extent to surface capabilities.
 */
VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps,
                       uint32_t width, uint32_t height) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    VkExtent2D extent = {width, height};
    extent.width  = std::clamp(extent.width,
                              caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height,
                              caps.minImageExtent.height,
                              caps.maxImageExtent.height);
    return extent;
}

} // namespace

VkSwapchain::~VkSwapchain() {
    Shutdown();
}

bool VkSwapchain::Init(VkPhysicalDevice physicalDevice,
                       VkDevice         device,
                       VkSurfaceKHR     surface,
                       const QueueFamilyIndices& indices,
                       uint32_t         width,
                       uint32_t         height) {
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_surface        = surface;
    m_indices        = indices;

    // Query surface capabilities, formats, present modes.
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            m_physicalDevice, m_surface, &caps) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &formatCount, nullptr);
    if (formatCount == 0) {
        LOG_ERROR(Render, "No surface formats available");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &formatCount, formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_physicalDevice, m_surface, &presentModeCount, nullptr);
    if (presentModeCount == 0) {
        LOG_ERROR(Render, "No present modes available");
        return false;
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    const VkPresentModeKHR  presentMode   = ChoosePresentMode(presentModes);
    m_extent = ChooseExtent(caps, width, height);
    m_format = surfaceFormat.format;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    std::array<uint32_t, 2> queueFamilyIndices = {
        m_indices.graphicsFamily,
        m_indices.presentFamily
    };
    const bool exclusive = m_indices.sameFamily;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = m_surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = surfaceFormat.format;
    sci.imageColorSpace  = surfaceFormat.colorSpace;
    sci.imageExtent      = m_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = exclusive ? VK_SHARING_MODE_EXCLUSIVE
                                    : VK_SHARING_MODE_CONCURRENT;
    if (!exclusive) {
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices   = queueFamilyIndices.data();
    }
    sci.preTransform   = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode    = presentMode;
    sci.clipped       = VK_TRUE;
    sci.oldSwapchain  = VK_NULL_HANDLE;

    const VkResult res = vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain);
    if (res != VK_SUCCESS || m_swapchain == VK_NULL_HANDLE) {
        LOG_ERROR(Render, "vkCreateSwapchainKHR failed (code {})", static_cast<int>(res));
        return false;
    }

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr);
    m_images.resize(swapchainImageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, m_images.data());

    m_imageViews.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image    = m_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format   = m_format;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel   = 0;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(m_device, &ivci, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkCreateImageView failed for swapchain image {}", i);
            DestroySwapchainObjects();
            return false;
        }
    }

    // Render pass: 1 color attachment, clear→store, final PRESENT_SRC_KHR.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_format;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &colorAttachment;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;

    if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkCreateRenderPass failed");
        DestroySwapchainObjects();
        return false;
    }

    m_framebuffers.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = m_renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &m_imageViews[i];
        fbci.width           = m_extent.width;
        fbci.height          = m_extent.height;
        fbci.layers          = 1;

        if (vkCreateFramebuffer(m_device, &fbci, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkCreateFramebuffer failed for image {}", i);
            DestroySwapchainObjects();
            return false;
        }
    }

    LOG_INFO(Render, "Swapchain created — {}×{} format={} images={}",
             m_extent.width, m_extent.height,
             static_cast<int>(m_format),
             swapchainImageCount);

    return true;
}

void VkSwapchain::Shutdown() {
    DestroySwapchainObjects();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_surface        = VK_NULL_HANDLE;
}

bool VkSwapchain::Recreate(uint32_t width, uint32_t height) {
    if (m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE) {
        m_recreateRequested = false;
        return false;
    }

    vkDeviceWaitIdle(m_device);

    // Destroy framebuffers, image views, render pass (keep old swapchain for create).
    for (VkFramebuffer fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, fb, nullptr);
        }
    }
    m_framebuffers.clear();
    for (VkImageView iv : m_imageViews) {
        if (iv != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, iv, nullptr);
        }
    }
    m_imageViews.clear();
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    const VkSwapchainKHR oldSwapchain = m_swapchain;

    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            m_physicalDevice, m_surface, &caps) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed on recreate");
        m_recreateRequested = false;
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            m_physicalDevice, m_surface, &formatCount, formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            m_physicalDevice, m_surface, &presentModeCount, presentModes.data());
    }

    if (formatCount == 0 || presentModeCount == 0) {
        LOG_ERROR(Render, "No formats or present modes on recreate");
        m_recreateRequested = false;
        return false;
    }

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    const VkPresentModeKHR  presentMode   = ChoosePresentMode(presentModes);
    m_extent = ChooseExtent(caps, width, height);
    m_format = surfaceFormat.format;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    std::array<uint32_t, 2> queueFamilyIndices = {
        m_indices.graphicsFamily,
        m_indices.presentFamily
    };
    const bool exclusive = m_indices.sameFamily;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = m_surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = surfaceFormat.format;
    sci.imageColorSpace  = surfaceFormat.colorSpace;
    sci.imageExtent      = m_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = exclusive ? VK_SHARING_MODE_EXCLUSIVE
                                    : VK_SHARING_MODE_CONCURRENT;
    if (!exclusive) {
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices   = queueFamilyIndices.data();
    }
    sci.preTransform   = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode    = presentMode;
    sci.clipped        = VK_TRUE;
    sci.oldSwapchain   = oldSwapchain;

    const VkResult res = vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain);
    vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);
    if (res != VK_SUCCESS || m_swapchain == VK_NULL_HANDLE) {
        LOG_ERROR(Render, "vkCreateSwapchainKHR failed on recreate (code {})",
                  static_cast<int>(res));
        m_recreateRequested = false;
        return false;
    }

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr);
    m_images.resize(swapchainImageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, m_images.data());

    m_imageViews.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image    = m_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format   = m_format;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel   = 0;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(m_device, &ivci, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkCreateImageView failed on recreate for image {}", i);
            DestroySwapchainObjects();
            m_recreateRequested = false;
            return false;
        }
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_format;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &colorAttachment;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;

    if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkCreateRenderPass failed on recreate");
        DestroySwapchainObjects();
        m_recreateRequested = false;
        return false;
    }

    m_framebuffers.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = m_renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &m_imageViews[i];
        fbci.width           = m_extent.width;
        fbci.height          = m_extent.height;
        fbci.layers          = 1;

        if (vkCreateFramebuffer(m_device, &fbci, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkCreateFramebuffer failed on recreate for image {}", i);
            DestroySwapchainObjects();
            m_recreateRequested = false;
            return false;
        }
    }

    LOG_INFO(Render, "Swapchain recreated — {}×{}", m_extent.width, m_extent.height);
    m_recreateRequested = false;
    return true;
}

void VkSwapchain::DestroySwapchainObjects() {
    if (m_device == VK_NULL_HANDLE) { return; }

    for (VkFramebuffer fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, fb, nullptr);
        }
    }
    m_framebuffers.clear();

    for (VkImageView iv : m_imageViews) {
        if (iv != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, iv, nullptr);
        }
    }
    m_imageViews.clear();
    m_images.clear();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
        LOG_INFO(Render, "Swapchain destroyed");
    }
}

} // namespace engine::render::vk
