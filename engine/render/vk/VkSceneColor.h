#pragma once

/**
 * @file VkSceneColor.h
 * @brief Offscreen color target (SceneColor) for frame graph; copy/blit to swapchain.
 *
 * Ticket: M02.4 — Offscreen SceneColor + blit/copy vers swapchain.
 *
 * Creates an image with extent matching the swapchain, plus render pass and
 * framebuffer for clear/draw. Recreate on resize.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Offscreen SceneColor image (render target + transfer source).
 *
 * Init after swapchain; Recreate when swapchain is recreated.
 */
class VkSceneColor {
public:
    VkSceneColor() = default;

    ~VkSceneColor();

    VkSceneColor(const VkSceneColor&) = delete;
    VkSceneColor& operator=(const VkSceneColor&) = delete;
    VkSceneColor(VkSceneColor&&) = delete;
    VkSceneColor& operator=(VkSceneColor&&) = delete;

    /**
     * @brief Creates offscreen image, view, render pass and framebuffer.
     *
     * @param physicalDevice  Physical device (for memory props).
     * @param device          Logical device.
     * @param width           Image width (matches swapchain extent).
     * @param height          Image height.
     * @param format          Image format (e.g. swapchain format).
     * @return                true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            uint32_t         width,
                            uint32_t         height,
                            VkFormat         format);

    /**
     * @brief Recreates image with new extent (call on resize after swapchain recreate).
     */
    [[nodiscard]] bool Recreate(uint32_t width, uint32_t height);

    /**
     * @brief Destroys image, view, render pass and framebuffer.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_image != VK_NULL_HANDLE; }
    [[nodiscard]] VkImage GetImage() const noexcept { return m_image; }
    [[nodiscard]] VkImageView GetImageView() const noexcept { return m_imageView; }
    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return m_renderPass; }
    [[nodiscard]] VkFramebuffer GetFramebuffer() const noexcept { return m_framebuffer; }
    [[nodiscard]] VkExtent2D Extent() const noexcept { return m_extent; }
    [[nodiscard]] VkFormat Format() const noexcept { return m_format; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkImage          m_image          = VK_NULL_HANDLE;
    VkDeviceMemory   m_memory         = VK_NULL_HANDLE;
    VkImageView      m_imageView      = VK_NULL_HANDLE;
    VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
    VkFramebuffer    m_framebuffer    = VK_NULL_HANDLE;
    VkExtent2D       m_extent         = {0, 0};
    VkFormat         m_format         = VK_FORMAT_UNDEFINED;
};

} // namespace engine::render::vk
