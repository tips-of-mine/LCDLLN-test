#pragma once

/**
 * @file VkBloomPyramid.h
 * @brief Bloom mip pyramid (1/2 .. 1/32) for prefilter + downsample chain.
 *
 * Ticket: M08.1 — Bloom: prefilter + downsample pyramid.
 *
 * 5 levels: Mip0 = 1/2 res, Mip1 = 1/4, Mip2 = 1/8, Mip3 = 1/16, Mip4 = 1/32.
 * Format R16G16B16A16_SFLOAT. Each level has image, view, render pass, framebuffer.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/** Number of bloom mip levels (1/2, 1/4, 1/8, 1/16, 1/32). */
constexpr uint32_t kBloomMipCount = 5u;

/**
 * @brief Bloom pyramid: 5 mip images (1/2 to 1/32 of full extent).
 *
 * Init(fullWidth, fullHeight) builds mips at (w/2,h/2), (w/4,h/4), ..., (w/32,h/32).
 */
class VkBloomPyramid {
public:
    VkBloomPyramid() = default;

    ~VkBloomPyramid();

    VkBloomPyramid(const VkBloomPyramid&) = delete;
    VkBloomPyramid& operator=(const VkBloomPyramid&) = delete;

    /**
     * @brief Creates 5 mip images (and views, render passes, framebuffers).
     *
     * @param physicalDevice  Physical device.
     * @param device          Logical device.
     * @param fullWidth       Source (e.g. HDR) width.
     * @param fullHeight      Source height.
     * @return                true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            uint32_t         fullWidth,
                            uint32_t         fullHeight);

    /**
     * @brief Recreates pyramid with new full extent (call on resize).
     */
    [[nodiscard]] bool Recreate(uint32_t fullWidth, uint32_t fullHeight);

    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_images[0] != VK_NULL_HANDLE; }

    /** @brief Mip level extent (level 0 = 1/2, level 4 = 1/32). */
    [[nodiscard]] VkExtent2D GetExtent(uint32_t level) const noexcept;
    [[nodiscard]] VkImage GetImage(uint32_t level) const noexcept;
    [[nodiscard]] VkImageView GetView(uint32_t level) const noexcept;
    [[nodiscard]] VkRenderPass GetRenderPass(uint32_t level) const noexcept;
    [[nodiscard]] VkFramebuffer GetFramebuffer(uint32_t level) const noexcept;

    /** @brief Render pass with LOAD for additive upsample (M08.2). */
    [[nodiscard]] VkRenderPass GetUpsampleRenderPass(uint32_t level) const noexcept;
    [[nodiscard]] VkFramebuffer GetUpsampleFramebuffer(uint32_t level) const noexcept;

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    uint32_t         m_fullWidth       = 0u;
    uint32_t         m_fullHeight      = 0u;

    VkImage          m_images[kBloomMipCount]{};
    VkDeviceMemory   m_memory[kBloomMipCount]{};
    VkImageView      m_views[kBloomMipCount]{};
    VkRenderPass     m_renderPasses[kBloomMipCount]{};
    VkFramebuffer    m_framebuffers[kBloomMipCount]{};
    VkRenderPass     m_upsampleRenderPasses[kBloomMipCount]{};
    VkFramebuffer    m_upsampleFramebuffers[kBloomMipCount]{};
    VkExtent2D       m_extents[kBloomMipCount]{};
};

} // namespace engine::render::vk
