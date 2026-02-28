#pragma once

/**
 * @file VkGBuffer.h
 * @brief Deferred GBuffer: 3 color attachments (A/B/C) + depth for geometry pass.
 *
 * Ticket: M03.1 — Deferred: GBuffer resources + geometry pass.
 *
 * Formats: A=R8G8B8A8_SRGB (albedo), B=A2B10G10R10_UNORM (normal),
 * C=R8G8B8A8_UNORM (ORM), D=R16G16_SFLOAT (velocity, M07.3), Depth=D32_SFLOAT. Recreate on resize.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief GBuffer: 4 color images (A/B/C/Velocity) + 1 depth image, render pass and framebuffer.
 */
class VkGBuffer {
public:
    VkGBuffer() = default;

    ~VkGBuffer();

    VkGBuffer(const VkGBuffer&) = delete;
    VkGBuffer& operator=(const VkGBuffer&) = delete;
    VkGBuffer(VkGBuffer&&) = delete;
    VkGBuffer& operator=(VkGBuffer&&) = delete;

    /**
     * @brief Creates GBuffer images (A, B, C, Depth), render pass and framebuffer.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            uint32_t         width,
                            uint32_t         height);

    /**
     * @brief Recreates GBuffer with new extent (call on resize).
     */
    [[nodiscard]] bool Recreate(uint32_t width, uint32_t height);

    /**
     * @brief Destroys all GBuffer resources.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_imageA != VK_NULL_HANDLE; }

    [[nodiscard]] VkImage     GetImageA() const noexcept { return m_imageA; }
    [[nodiscard]] VkImage     GetImageB() const noexcept { return m_imageB; }
    [[nodiscard]] VkImage     GetImageC() const noexcept { return m_imageC; }
    [[nodiscard]] VkImage     GetImageD() const noexcept { return m_imageD; }
    [[nodiscard]] VkImage     GetImageDepth() const noexcept { return m_imageDepth; }
    [[nodiscard]] VkImageView GetViewA() const noexcept { return m_viewA; }
    [[nodiscard]] VkImageView GetViewB() const noexcept { return m_viewB; }
    [[nodiscard]] VkImageView GetViewC() const noexcept { return m_viewC; }
    [[nodiscard]] VkImageView GetViewD() const noexcept { return m_viewD; }
    [[nodiscard]] VkImageView GetViewDepth() const noexcept { return m_viewDepth; }

    [[nodiscard]] VkRenderPass    GetRenderPass() const noexcept { return m_renderPass; }
    [[nodiscard]] VkFramebuffer   GetFramebuffer() const noexcept { return m_framebuffer; }
    [[nodiscard]] VkExtent2D      Extent() const noexcept { return m_extent; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkExtent2D       m_extent          = {0, 0};

    VkImage        m_imageA     = VK_NULL_HANDLE;
    VkDeviceMemory m_memoryA    = VK_NULL_HANDLE;
    VkImageView    m_viewA     = VK_NULL_HANDLE;

    VkImage        m_imageB     = VK_NULL_HANDLE;
    VkDeviceMemory m_memoryB    = VK_NULL_HANDLE;
    VkImageView    m_viewB     = VK_NULL_HANDLE;

    VkImage        m_imageC     = VK_NULL_HANDLE;
    VkDeviceMemory m_memoryC   = VK_NULL_HANDLE;
    VkImageView    m_viewC     = VK_NULL_HANDLE;

    VkImage        m_imageD     = VK_NULL_HANDLE;
    VkDeviceMemory m_memoryD    = VK_NULL_HANDLE;
    VkImageView    m_viewD      = VK_NULL_HANDLE;

    VkImage        m_imageDepth = VK_NULL_HANDLE;
    VkDeviceMemory m_memoryDepth = VK_NULL_HANDLE;
    VkImageView    m_viewDepth  = VK_NULL_HANDLE;

    VkRenderPass   m_renderPass   = VK_NULL_HANDLE;
    VkFramebuffer  m_framebuffer  = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
