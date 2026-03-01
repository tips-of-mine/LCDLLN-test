/**
 * @file VkTimestampPool.cpp
 * @brief GPU timestamp query pool implementation (M18.1).
 */

#include "engine/render/vk/VkTimestampPool.h"
#include "engine/core/Log.h"

#include <cstring>
#include <vector>

namespace engine::render::vk {

bool VkTimestampPool::Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxPasses) {
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || maxPasses == 0u) {
        return false;
    }
    m_device = device;
    m_count  = 2u * maxPasses;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    m_timestampPeriodNs = static_cast<double>(props.limits.timestampPeriod);

    VkQueryPoolCreateInfo qpci{};
    qpci.sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType          = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount         = m_count;
    if (vkCreateQueryPool(device, &qpci, nullptr, &m_pool) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkTimestampPool: vkCreateQueryPool failed");
        m_device = VK_NULL_HANDLE;
        m_count  = 0u;
        return false;
    }
    return true;
}

void VkTimestampPool::Shutdown() {
    if (m_device != VK_NULL_HANDLE && m_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_pool, nullptr);
        m_pool   = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
        m_count  = 0u;
    }
}

void VkTimestampPool::Reset(VkCommandBuffer cmd) const {
    if (m_pool != VK_NULL_HANDLE && cmd != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, m_pool, 0, m_count);
    }
}

bool VkTimestampPool::GetResults(VkDevice device, uint64_t* outNs) const {
    if (m_pool == VK_NULL_HANDLE || outNs == nullptr || m_count == 0u) {
        return false;
    }
    std::vector<uint64_t> raw(m_count);
    const VkResult res = vkGetQueryPoolResults(
        device,
        m_pool,
        0,
        m_count,
        raw.size() * sizeof(uint64_t),
        raw.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (res != VK_SUCCESS) {
        return false;
    }
    for (uint32_t i = 0; i < m_count; ++i) {
        outNs[i] = static_cast<uint64_t>(static_cast<double>(raw[i]) * m_timestampPeriodNs);
    }
    return true;
}

} // namespace engine::render::vk
