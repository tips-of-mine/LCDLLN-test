#pragma once

/**
 * @file VkParticlePass.h
 * @brief Render pass + framebuffer for particles: HDR color (load+blend) + depth (load, read-only). M17.2.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Particle pass: color attachment (HDR, load op LOAD, blend), depth attachment (load, depth read-only).
 * Framebuffer binds external HDR image view and depth image view (same extent).
 */
class VkParticlePass {
public:
    VkParticlePass() = default;
    ~VkParticlePass();

    VkParticlePass(const VkParticlePass&) = delete;
    VkParticlePass& operator=(const VkParticlePass&) = delete;

    /**
     * @brief Creates render pass (color load+store+blend, depth load+store) and framebuffer.
     * @param device Vulkan device.
     * @param width Extent width (must match HDR and depth).
     * @param height Extent height.
     * @param hdrImageView SceneColorHDR image view (R16G16B16A16_SFLOAT).
     * @param depthImageView GBuffer depth view (D32_SFLOAT).
     */
    [[nodiscard]] bool Init(VkDevice device,
                           uint32_t width,
                           uint32_t height,
                           VkImageView hdrImageView,
                           VkImageView depthImageView);

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
