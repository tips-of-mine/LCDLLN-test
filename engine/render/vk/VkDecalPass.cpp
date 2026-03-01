/**
 * @file VkDecalPass.cpp
 * @brief Decal render pass: GBuffer A load + depth load. M17.3.
 */

#include "engine/render/vk/VkDecalPass.h"
#include "engine/core/Log.h"

#include <array>

namespace engine::render::vk {

VkDecalPass::~VkDecalPass() {
    Shutdown();
}

void VkDecalPass::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

bool VkDecalPass::Init(VkDevice device, uint32_t width, uint32_t height,
                       VkImageView gbufferAView, VkImageView depthView) {
    if (device == VK_NULL_HANDLE || width == 0 || height == 0 ||
        gbufferAView == VK_NULL_HANDLE || depthView == VK_NULL_HANDLE) {
        return false;
    }
    Shutdown();
    m_device = device;
    m_extent = {width, height};

    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_SRGB;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkDecalPass: vkCreateRenderPass failed");
        DestroyResources();
        return false;
    }

    std::array<VkImageView, 2> fbAttachments = {gbufferAView, depthView};
    VkFramebufferCreateInfo fbci{};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = m_renderPass;
    fbci.attachmentCount = 2;
    fbci.pAttachments = fbAttachments.data();
    fbci.width = width;
    fbci.height = height;
    fbci.layers = 1;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &m_framebuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkDecalPass: vkCreateFramebuffer failed");
        DestroyResources();
        return false;
    }
    return true;
}

void VkDecalPass::Shutdown() {
    DestroyResources();
    m_device = VK_NULL_HANDLE;
    m_extent = {0, 0};
}

} // namespace engine::render::vk
