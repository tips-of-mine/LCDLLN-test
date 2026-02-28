#pragma once

/**
 * @file VkSwapchain.h
 * @brief Vulkan swapchain, image views, render pass and framebuffers.
 *
 * Ticket: M01.3 — Vulkan: swapchain, image views, render pass, framebuffers.
 *
 * Responsibilities:
 *   - Create and own a VkSwapchainKHR (extent = window size).
 *   - Create image views for each swapchain image.
 *   - Create a render pass with one color attachment (clear→present).
 *   - Create one framebuffer per swapchain image.
 *   - Recreate on window resize (flag + recreate).
 */

#include "engine/render/vk/VkDeviceContext.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief RAII wrapper for swapchain, image views, render pass and framebuffers.
 *
 * Typical usage:
 *   VkSwapchain swap;
 *   swap.Init(physicalDevice, device, surface, indices, width, height);
 *   // On resize: swap.RequestRecreate(); then in render loop:
 *   if (swap.NeedsRecreate()) swap.Recreate(width, height);
 *   swap.Shutdown();
 */
class VkSwapchain {
public:
    /// Default constructor creates an invalid, empty swapchain.
    VkSwapchain() = default;

    /// Destructor automatically calls Shutdown() if needed.
    ~VkSwapchain();

    // Non-copyable, non-movable.
    VkSwapchain(const VkSwapchain&)            = delete;
    VkSwapchain& operator=(const VkSwapchain&) = delete;
    VkSwapchain(VkSwapchain&&)                 = delete;
    VkSwapchain& operator=(VkSwapchain&&)      = delete;

    /**
     * @brief Creates swapchain, image views, render pass and framebuffers.
     *
     * Format: SRGB if available, else first supported.
     * Present mode: MAILBOX if available, else FIFO.
     *
     * @param physicalDevice  Selected physical device.
     * @param device          Logical device.
     * @param surface         Window surface.
     * @param indices         Queue family indices (graphics, present).
     * @param width           Framebuffer width in pixels.
     * @param height          Framebuffer height in pixels.
     * @return                true on success, false on error.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                           VkDevice         device,
                           VkSurfaceKHR     surface,
                           const QueueFamilyIndices& indices,
                           uint32_t         width,
                           uint32_t         height);

    /**
     * @brief Destroys framebuffers, image views, render pass and swapchain.
     */
    void Shutdown();

    /**
     * @brief Marks that a recreate is needed (e.g. after window resize).
     */
    void RequestRecreate() noexcept { m_recreateRequested = true; }

    /**
     * @brief Returns true if RequestRecreate() was called.
     */
    [[nodiscard]] bool NeedsRecreate() const noexcept { return m_recreateRequested; }

    /**
     * @brief Recreates swapchain and dependent objects with new dimensions.
     *
     * Call when NeedsRecreate() is true. Clears the recreate flag on success.
     *
     * @param width   New framebuffer width.
     * @param height  New framebuffer height.
     * @return        true on success, false on error.
     */
    [[nodiscard]] bool Recreate(uint32_t width, uint32_t height);

    /// Returns true if the swapchain is valid.
    [[nodiscard]] bool IsValid() const noexcept { return m_swapchain != VK_NULL_HANDLE; }

    /// Returns the swapchain handle.
    [[nodiscard]] VkSwapchainKHR Get() const noexcept { return m_swapchain; }

    /// Returns the image format.
    [[nodiscard]] VkFormat Format() const noexcept { return m_format; }

    /// Returns the image extent.
    [[nodiscard]] VkExtent2D Extent() const noexcept { return m_extent; }

    /// Returns the number of swapchain images.
    [[nodiscard]] uint32_t ImageCount() const noexcept {
        return static_cast<uint32_t>(m_imageViews.size());
    }

    /// Returns the render pass handle.
    [[nodiscard]] VkRenderPass RenderPass() const noexcept { return m_renderPass; }

    /// Returns the framebuffer for image index i.
    [[nodiscard]] VkFramebuffer Framebuffer(uint32_t i) const noexcept {
        return (i < m_framebuffers.size()) ? m_framebuffers[i] : VK_NULL_HANDLE;
    }

private:
    /// Destroys only swapchain-dependent objects (views, framebuffers, swapchain).
    /// Keeps physical device, device, surface, indices for Recreate.
    void DestroySwapchainObjects();

    VkPhysicalDevice       m_physicalDevice = VK_NULL_HANDLE;
    VkDevice               m_device         = VK_NULL_HANDLE;
    VkSurfaceKHR           m_surface        = VK_NULL_HANDLE;
    QueueFamilyIndices     m_indices{};

    VkSwapchainKHR         m_swapchain      = VK_NULL_HANDLE;
    VkFormat               m_format         = VK_FORMAT_UNDEFINED;
    VkExtent2D             m_extent         = {0, 0};
    VkRenderPass           m_renderPass     = VK_NULL_HANDLE;
    std::vector<VkImageView>    m_imageViews;
    std::vector<VkFramebuffer>  m_framebuffers;

    bool                   m_recreateRequested = false;
};

} // namespace engine::render::vk
