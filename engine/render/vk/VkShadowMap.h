#pragma once

/**
 * @file VkShadowMap.h
 * @brief Cascaded shadow maps: 4x D32 depth images, one render pass per cascade.
 *
 * Ticket: M04.2 — Shadow pass: depth-only render per cascade.
 *
 * Resolution configurable (e.g. via config). Each cascade has its own image,
 * image view and framebuffer. Single depth-only render pass layout.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/** Number of shadow map cascades (must match Csm.h kCsmCascadeCount). */
constexpr uint32_t kShadowMapCascadeCount = 4;

/**
 * @brief Shadow map atlas or array: 4 depth images (D32_SFLOAT), render pass, framebuffers.
 */
class VkShadowMap {
public:
    VkShadowMap() = default;

    ~VkShadowMap();

    VkShadowMap(const VkShadowMap&) = delete;
    VkShadowMap& operator=(const VkShadowMap&) = delete;
    VkShadowMap(VkShadowMap&&) = delete;
    VkShadowMap& operator=(VkShadowMap&&) = delete;

    /**
     * @brief Creates 4 cascade depth images (D32_SFLOAT), render pass and framebuffers.
     *
     * @param physicalDevice Physical device.
     * @param device         Logical device.
     * @param size           Resolution (width and height) of each cascade texture.
     * @return               true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            uint32_t         size);

    /**
     * @brief Recreates shadow maps with new size (e.g. on config change).
     */
    [[nodiscard]] bool Recreate(uint32_t size);

    /**
     * @brief Destroys all resources.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_images[0] != VK_NULL_HANDLE; }

    /** @brief Get image for cascade index (0..kShadowMapCascadeCount-1). */
    [[nodiscard]] VkImage     GetImage(uint32_t cascadeIndex) const;
    /** @brief Get image view for cascade index (for sampling in lighting). */
    [[nodiscard]] VkImageView GetView(uint32_t cascadeIndex) const;
    /** @brief Get framebuffer for cascade index. */
    [[nodiscard]] VkFramebuffer GetFramebuffer(uint32_t cascadeIndex) const;

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return m_renderPass; }
    [[nodiscard]] uint32_t     Size() const noexcept { return m_size; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    uint32_t         m_size           = 0;

    VkImage        m_images[kShadowMapCascadeCount]{};
    VkDeviceMemory m_memories[kShadowMapCascadeCount]{};
    VkImageView    m_views[kShadowMapCascadeCount]{};
    VkFramebuffer  m_framebuffers[kShadowMapCascadeCount]{};
    VkRenderPass   m_renderPass = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
