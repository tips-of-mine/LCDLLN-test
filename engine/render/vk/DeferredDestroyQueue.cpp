/**
 * @file DeferredDestroyQueue.cpp
 * @brief Deferred GPU destroy: collect when fence signaled (M10.3).
 */

#include "engine/render/vk/DeferredDestroyQueue.h"

namespace engine::render::vk {

void DeferredDestroyQueue::Push(VkDevice device, VkFence fence, std::function<void()> onSignaled) {
    if (device == VK_NULL_HANDLE || fence == VK_NULL_HANDLE || !onSignaled) return;
    m_entries.push_back({ device, fence, std::move(onSignaled) });
}

void DeferredDestroyQueue::Collect(VkDevice device) {
    if (device == VK_NULL_HANDLE) return;
    size_t write = 0;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        Entry& e = m_entries[i];
        if (e.device != device) {
            if (write != i) m_entries[write] = std::move(e);
            ++write;
            continue;
        }
        VkResult res = vkGetFenceStatus(e.device, e.fence);
        if (res == VK_SUCCESS) {
            if (e.onSignaled) e.onSignaled();
        } else {
            if (write != i) m_entries[write] = std::move(e);
            ++write;
        }
    }
    m_entries.resize(write);
}

} // namespace engine::render::vk
