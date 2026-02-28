#pragma once

/**
 * @file VkTaaHistory.h
 * @brief TAA history ping-pong textures (HistoryA / HistoryB).
 *
 * Ticket: M07.2 — TAA: history ping-pong textures.
 *
 * Two images with same format and extent as SceneColor LDR (TAA input format).
 * Used for prev/next ping-pong; no render pass (copy/sample only).
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Two history images (HistoryA, HistoryB) for TAA ping-pong.
 *
 * Format and extent match SceneColor LDR. Usage: TRANSFER_DST, SAMPLED
 * (copy from current, sample in TAA pass).
 */
class VkTaaHistory {
public:
    VkTaaHistory() = default;

    ~VkTaaHistory();

    VkTaaHistory(const VkTaaHistory&) = delete;
    VkTaaHistory& operator=(const VkTaaHistory&) = delete;
    VkTaaHistory(VkTaaHistory&&) = delete;
    VkTaaHistory& operator=(VkTaaHistory&&) = delete;

    /**
     * @brief Creates two images and views (HistoryA = index 0, HistoryB = index 1).
     *
     * @param physicalDevice  Physical device (for memory props).
     * @param device          Logical device.
     * @param width           Image width (matches swapchain/extent).
     * @param height          Image height.
     * @param format          Image format (same as SceneColor LDR / TAA input).
     * @return                true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            uint32_t         width,
                            uint32_t         height,
                            VkFormat         format);

    /**
     * @brief Recreates both images with new extent (call on resize).
     */
    [[nodiscard]] bool Recreate(uint32_t width, uint32_t height);

    /**
     * @brief Destroys both images and views.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_images[0] != VK_NULL_HANDLE; }
    /**
     * @brief Returns image for index 0 (HistoryA) or 1 (HistoryB).
     */
    [[nodiscard]] VkImage GetImage(uint32_t index) const noexcept;
    /**
     * @brief Returns image view for index 0 (HistoryA) or 1 (HistoryB).
     */
    [[nodiscard]] VkImageView GetView(uint32_t index) const noexcept;
    [[nodiscard]] VkExtent2D Extent() const noexcept { return m_extent; }
    [[nodiscard]] VkFormat Format() const noexcept { return m_format; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkImage          m_images[2]      = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory   m_memory[2]     = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView      m_views[2]      = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkExtent2D       m_extent        = {0, 0};
    VkFormat         m_format        = VK_FORMAT_UNDEFINED;
};

} // namespace engine::render::vk
