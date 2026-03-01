#pragma once

/**
 * @file VkTimestampPool.h
 * @brief GPU timestamp query pool for frame-graph pass timing (M18.1).
 *
 * Provides 2 queries per pass (begin/end). Reset each frame before recording;
 * read results after the frame's fence has signalled.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Vulkan timestamp query pool for per-pass GPU timing.
 *
 * Holds 2 * maxPasses query slots (begin and end per pass). Recycle: reset
 * before each frame, read results after fence wait.
 */
class VkTimestampPool {
public:
    VkTimestampPool() = default;
    ~VkTimestampPool() = default;

    VkTimestampPool(const VkTimestampPool&) = delete;
    VkTimestampPool& operator=(const VkTimestampPool&) = delete;

    /**
     * @brief Creates a timestamp query pool with 2 * maxPasses slots.
     *
     * @param device          Logical device.
     * @param physicalDevice  Physical device (for timestamp period).
     * @param maxPasses       Maximum number of frame-graph passes to time.
     * @return                true on success.
     */
    [[nodiscard]] bool Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxPasses);

    /**
     * @brief Destroys the query pool.
     */
    void Shutdown();

    /**
     * @brief Returns true if the pool was created successfully.
     */
    [[nodiscard]] bool IsValid() const noexcept { return m_pool != VK_NULL_HANDLE; }

    /**
     * @brief Resets the query pool; call at the start of command buffer recording.
     *
     * @param cmd Command buffer that will record timestamp writes.
     */
    void Reset(VkCommandBuffer cmd) const;

    /**
     * @brief Returns the Vulkan query pool handle.
     */
    [[nodiscard]] VkQueryPool GetPool() const noexcept { return m_pool; }

    /**
     * @brief Returns the number of timestamp slots (2 * maxPasses).
     */
    [[nodiscard]] uint32_t GetCount() const noexcept { return m_count; }

    /**
     * @brief Returns the timestamp period in nanoseconds (device-dependent).
     */
    [[nodiscard]] double GetTimestampPeriodNs() const noexcept { return m_timestampPeriodNs; }

    /**
     * @brief Retrieves query results after the frame's fence has signalled.
     *
     * Converts raw timestamps to nanoseconds using the device period.
     * Call only after vkWaitForFences for the frame that recorded the queries.
     *
     * @param device   Logical device.
     * @param outNs    Buffer of size at least GetCount(); filled with timestamps in ns.
     * @return         true if results were read successfully.
     */
    [[nodiscard]] bool GetResults(VkDevice device, uint64_t* outNs) const;

private:
    VkDevice         m_device = VK_NULL_HANDLE;
    VkQueryPool      m_pool   = VK_NULL_HANDLE;
    uint32_t         m_count  = 0u;
    double           m_timestampPeriodNs = 1.0;
};

} // namespace engine::render::vk
