#pragma once

/**
 * @file VkDeviceContext.h
 * @brief Vulkan physical device selection, logical device and queues.
 *
 * Ticket: M01.2 — Vulkan: physical device selection, logical device, queues.
 *
 * Responsibilities:
 *   - Enumerate physical GPUs and select one that supports graphics + present
 *     on the given surface and has VK_KHR_swapchain support.
 *   - Prefer a discrete GPU when available.
 *   - Create a VkDevice and retrieve graphics/present queues.
 *   - Expose queue family indices and queue handles to the rest of the engine.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Queue family indices used by the logical device.
 *
 * Both graphics and present families must be valid (set to a concrete index).
 * They may be identical (single queue family supporting both roles).
 */
struct QueueFamilyIndices {
    /// Index of a queue family supporting VK_QUEUE_GRAPHICS_BIT.
    std::uint32_t graphicsFamily = 0;
    /// Index of a queue family supporting presentation to the swapchain surface.
    std::uint32_t presentFamily  = 0;
    /// True if graphicsFamily == presentFamily.
    bool           sameFamily    = true;
};

/**
 * @brief RAII wrapper owning a Vulkan physical device, logical device and queues.
 *
 * Typical usage:
 *   VkDeviceContext ctx;
 *   ctx.Init(instance, surface);
 *   // Use ctx.PhysicalDevice(), ctx.Device(), ctx.GraphicsQueue(), ctx.PresentQueue().
 *   ctx.Shutdown();
 */
class VkDeviceContext {
public:
    /// Default constructor creates an invalid, empty context.
    VkDeviceContext() = default;

    /// Destructor automatically calls Shutdown() if needed.
    ~VkDeviceContext();

    // Non-copyable, non-movable.
    VkDeviceContext(const VkDeviceContext&)            = delete;
    VkDeviceContext& operator=(const VkDeviceContext&) = delete;
    VkDeviceContext(VkDeviceContext&&)                 = delete;
    VkDeviceContext& operator=(VkDeviceContext&&)      = delete;

    /**
     * @brief Initialises the context from an instance and surface.
     *
     * Steps:
     *   - Enumerate physical devices for @p instance.
     *   - Score and choose one that supports graphics + present on @p surface,
     *     supports VK_KHR_swapchain and has at least one surface format and
     *     one present mode.
     *   - Create a VkDevice with the required queues.
     *   - Retrieve graphics and present VkQueue handles.
     *
     * @param instance Valid VkInstance created by the engine.
     * @param surface  Valid VkSurfaceKHR matching the current window.
     * @return         true on success, false on unrecoverable error.
     */
    [[nodiscard]] bool Init(VkInstance instance, VkSurfaceKHR surface);

    /**
     * @brief Destroys the logical device (if any).
     *
     * The physical device handle remains owned by the Vulkan instance and
     * does not need explicit destruction.
     */
    void Shutdown();

    /// Returns true if a logical device has been successfully created.
    [[nodiscard]] bool IsValid() const noexcept { return m_device != VK_NULL_HANDLE; }

    /// Returns the selected VkPhysicalDevice.
    [[nodiscard]] VkPhysicalDevice PhysicalDevice() const noexcept { return m_physicalDevice; }

    /// Returns the logical VkDevice.
    [[nodiscard]] VkDevice Device() const noexcept { return m_device; }

    /// Returns the graphics queue handle.
    [[nodiscard]] VkQueue GraphicsQueue() const noexcept { return m_graphicsQueue; }

    /// Returns the present queue handle.
    [[nodiscard]] VkQueue PresentQueue() const noexcept { return m_presentQueue; }

    /// Returns the queue family indices used by this device.
    [[nodiscard]] const QueueFamilyIndices& Indices() const noexcept { return m_indices; }

private:
    VkInstance        m_instance       = VK_NULL_HANDLE;
    VkSurfaceKHR      m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice  m_physicalDevice = VK_NULL_HANDLE;
    VkDevice          m_device         = VK_NULL_HANDLE;
    VkQueue           m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue           m_presentQueue   = VK_NULL_HANDLE;
    QueueFamilyIndices m_indices{};
};

} // namespace engine::render::vk

