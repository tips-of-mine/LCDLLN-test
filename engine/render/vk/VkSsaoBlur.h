#pragma once

/**
 * @file VkSsaoBlur.h
 * @brief SSAO_Blur temp and output render targets (R16F) for bilateral blur passes.
 *
 * Ticket: M06.3 — SSAO: bilateral blur (2 passes).
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Two R16_SFLOAT images (temp + output), shared render pass, two framebuffers.
 */
class VkSsaoBlur {
public:
    VkSsaoBlur() = default;

    ~VkSsaoBlur();

    VkSsaoBlur(const VkSsaoBlur&) = delete;
    VkSsaoBlur& operator=(const VkSsaoBlur&) = delete;

    /**
     * @brief Creates temp and output R16_SFLOAT images, one render pass, two framebuffers.
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

    [[nodiscard]] bool IsValid() const noexcept { return m_imageTemp != VK_NULL_HANDLE; }

    [[nodiscard]] VkImage GetImageTemp() const noexcept { return m_imageTemp; }
    [[nodiscard]] VkImageView GetImageViewTemp() const noexcept { return m_viewTemp; }
    [[nodiscard]] VkFramebuffer GetFramebufferTemp() const noexcept { return m_framebufferTemp; }

    [[nodiscard]] VkImage GetImageOutput() const noexcept { return m_imageOutput; }
    [[nodiscard]] VkImageView GetImageViewOutput() const noexcept { return m_viewOutput; }
    [[nodiscard]] VkFramebuffer GetFramebufferOutput() const noexcept { return m_framebufferOutput; }

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return m_renderPass; }
    [[nodiscard]] VkExtent2D Extent() const noexcept { return m_extent; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkExtent2D       m_extent         = {0, 0};

    VkImage        m_imageTemp    = VK_NULL_HANDLE;
    VkDeviceMemory m_memoryTemp   = VK_NULL_HANDLE;
    VkImageView    m_viewTemp     = VK_NULL_HANDLE;
    VkFramebuffer  m_framebufferTemp = VK_NULL_HANDLE;

    VkImage        m_imageOutput  = VK_NULL_HANDLE;
    VkDeviceMemory m_memoryOutput = VK_NULL_HANDLE;
    VkImageView    m_viewOutput   = VK_NULL_HANDLE;
    VkFramebuffer  m_framebufferOutput = VK_NULL_HANDLE;

    VkRenderPass   m_renderPass   = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
