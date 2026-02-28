#pragma once

/**
 * @file VkTaaOutput.h
 * @brief Offscreen render target for TAA pass output (write then copy to history / present).
 *
 * Ticket: M07.4 — TAA pass: reprojection + clamp anti-ghost.
 *
 * Same format and extent as SceneColor LDR. TAA pass renders here; then copy to HistoryNext.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief TAA output image (render target + transfer source for copy/present).
 */
class VkTaaOutput {
public:
    VkTaaOutput() = default;

    ~VkTaaOutput();

    VkTaaOutput(const VkTaaOutput&) = delete;
    VkTaaOutput& operator=(const VkTaaOutput&) = delete;

    /**
     * @brief Creates image, view, render pass and framebuffer (same as SceneColor LDR).
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            uint32_t         width,
                            uint32_t         height,
                            VkFormat         format);

    [[nodiscard]] bool Recreate(uint32_t width, uint32_t height);
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
