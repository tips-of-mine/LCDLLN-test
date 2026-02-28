#pragma once

/**
 * @file DeferredDestroyQueue.h
 * @brief Deferred GPU resource destruction: queue (resource + fence), collect when fence signaled (M10.3).
 *
 * Push(resource, fence); Collect() at BeginFrame destroys resources whose fence has signaled.
 */

#include <vulkan/vulkan.h>

#include <functional>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Queue of GPU resources to destroy after their fence has signaled.
 *
 * Push(device, fence, callback). Collect(device) runs callbacks for signaled fences and removes them.
 * Call Collect() at BeginFrame.
 */
class DeferredDestroyQueue {
public:
    DeferredDestroyQueue() = default;

    /**
     * @brief Enqueues a destroy callback to run when the fence has signaled.
     *
     * @param device   Vulkan device (used when collecting to query fence).
     * @param fence    Fence that must be signaled before destroying.
     * @param onSignaled Callback that destroys the resource (e.g. vkDestroyBuffer, vkFreeMemory).
     */
    void Push(VkDevice device, VkFence fence, std::function<void()> onSignaled);

    /**
     * @brief Processes the queue: for each entry whose fence has signaled, runs the callback and removes it.
     *
     * Call at BeginFrame.
     */
    void Collect(VkDevice device);

private:
    struct Entry {
        VkDevice device = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::function<void()> onSignaled;
    };
    std::vector<Entry> m_entries;
};

} // namespace engine::render::vk
