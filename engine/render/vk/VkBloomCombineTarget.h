#pragma once

/**
 * @file VkBloomCombineTarget.h
 * @brief HDR render target for SceneColor_HDR + bloom*intensity (M08.2).
 *
 * Same format and extent as SceneColor_HDR. Tonemap reads from this when bloom is active.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Offscreen HDR target for combine pass output (scene + bloom).
 */
class VkBloomCombineTarget {
public:
    VkBloomCombineTarget() = default;

    ~VkBloomCombineTarget();

    VkBloomCombineTarget(const VkBloomCombineTarget&) = delete;
    VkBloomCombineTarget& operator=(const VkBloomCombineTarget&) = delete;

    /**
     * @brief Creates HDR image, view, render pass and framebuffer.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                           VkDevice         device,
                           uint32_t         width,
                           uint32_t         height);

    /**
     * @brief Recreates with new extent (call on resize).
     */
    [[nodiscard]] bool Recreate(uint32_t width, uint32_t height);

    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_image != VK_NULL_HANDLE; }
    [[nodiscard]] VkImage GetImage() const noexcept { return m_image; }
    [[nodiscard]] VkImageView GetImageView() const noexcept { return m_imageView; }
    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return m_renderPass; }
    [[nodiscard]] VkFramebuffer GetFramebuffer() const noexcept { return m_framebuffer; }
    [[nodiscard]] VkExtent2D Extent() const noexcept { return m_extent; }

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
};

} // namespace engine::render::vk
