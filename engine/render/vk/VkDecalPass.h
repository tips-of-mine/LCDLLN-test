#pragma once

/**
 * @file VkDecalPass.h
 * @brief Render pass for decals: GBuffer A (load) + depth (load). M17.3.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Decal pass: color attachment = GBuffer A (load, write albedo), depth (load, depth test).
 * Runs after geometry, before lighting.
 */
class VkDecalPass {
public:
    VkDecalPass() = default;
    ~VkDecalPass();

    VkDecalPass(const VkDecalPass&) = delete;
    VkDecalPass& operator=(const VkDecalPass&) = delete;

    /**
     * @brief Creates render pass and framebuffer (gbufferAView + depthView).
     */
    [[nodiscard]] bool Init(VkDevice device, uint32_t width, uint32_t height,
                            VkImageView gbufferAView, VkImageView depthView);

    void Shutdown();

    [[nodiscard]] bool IsValid() const { return m_renderPass != VK_NULL_HANDLE; }
    [[nodiscard]] VkRenderPass GetRenderPass() const { return m_renderPass; }
    [[nodiscard]] VkFramebuffer GetFramebuffer() const { return m_framebuffer; }
    [[nodiscard]] VkExtent2D Extent() const { return m_extent; }

private:
    void DestroyResources();

    VkDevice m_device = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkExtent2D m_extent = {0, 0};
};

} // namespace engine::render::vk
