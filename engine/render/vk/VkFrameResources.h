#pragma once

/**
 * @file VkFrameResources.h
 * @brief Per-frame command pools, command buffers, semaphores and fences.
 *
 * Ticket: M01.4 — Vulkan: command pools/buffers + sync (frames in flight) + clear.
 *
 * Responsibilities:
 *   - Own FrameResources[2]: cmdPool, cmdBuffer, imageAvailable semaphore,
 *     renderFinished semaphore, inFlight fence per frame.
 *   - Provide access for the render loop (acquire → record → submit → present).
 */

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace engine::render::vk {

/// Number of frames in flight (double buffering).
constexpr uint32_t kFramesInFlight = 2;

/**
 * @brief Per-frame Vulkan resources for the render loop.
 */
struct FrameResources {
    VkCommandPool   cmdPool              = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer           = VK_NULL_HANDLE;
    VkSemaphore     imageAvailable       = VK_NULL_HANDLE;
    VkSemaphore     renderFinished       = VK_NULL_HANDLE;
    VkFence         inFlightFence        = VK_NULL_HANDLE;
};

/**
 * @brief RAII wrapper for FrameResources[2].
 *
 * Typical usage:
 *   VkFrameResources frames;
 *   frames.Init(device, graphicsQueueFamilyIndex);
 *   // Each frame: use frames.Current(), frames.AdvanceFrame();
 *   frames.Shutdown();
 */
class VkFrameResources {
public:
    /// Default constructor.
    VkFrameResources() = default;

    /// Destructor calls Shutdown() if needed.
    ~VkFrameResources();

    // Non-copyable, non-movable.
    VkFrameResources(const VkFrameResources&)            = delete;
    VkFrameResources& operator=(const VkFrameResources&) = delete;
    VkFrameResources(VkFrameResources&&)                 = delete;
    VkFrameResources& operator=(VkFrameResources&&)       = delete;

    /**
     * @brief Creates command pools, command buffers, semaphores and fences.
     *
     * @param device                   Logical device.
     * @param graphicsQueueFamilyIndex Queue family for the command pool.
     * @return                        true on success, false on error.
     */
    [[nodiscard]] bool Init(VkDevice device, uint32_t graphicsQueueFamilyIndex);

    /**
     * @brief Destroys all frame resources.
     */
    void Shutdown();

    /// Returns true if resources are valid.
    [[nodiscard]] bool IsValid() const noexcept {
        return m_resources[0].cmdPool != VK_NULL_HANDLE;
    }

    /// Returns the resources for the current frame index.
    [[nodiscard]] FrameResources& Current() noexcept {
        return m_resources[m_currentFrame];
    }

    /// Returns the resources for the current frame index (const).
    [[nodiscard]] const FrameResources& Current() const noexcept {
        return m_resources[m_currentFrame];
    }

    /// Returns the current frame index (0 or 1).
    [[nodiscard]] uint32_t CurrentFrameIndex() const noexcept {
        return m_currentFrame;
    }

    /// Advances to the next frame index (wraps at 2).
    void AdvanceFrame() noexcept {
        m_currentFrame = (m_currentFrame + 1u) % kFramesInFlight;
    }

    /// Returns the device handle.
    [[nodiscard]] VkDevice Device() const noexcept { return m_device; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    std::array<FrameResources, kFramesInFlight> m_resources{};
    uint32_t m_currentFrame = 0;
};

} // namespace engine::render::vk
