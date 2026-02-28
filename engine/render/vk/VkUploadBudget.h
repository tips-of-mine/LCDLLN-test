#pragma once

/**
 * @file VkUploadBudget.h
 * @brief GPU upload budget per frame + persistent staging buffer ring + upload queue (M10.4).
 *
 * Limits uploads per frame (e.g. 32MB) to avoid stutter. Staging ring = persistent buffers
 * (one per frame in flight). Priority: terrain/HLOD first, then texture mips. Report stats.
 */

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Persistent staging buffer ring: one host-visible buffer per frame in flight.
 *
 * Staging allocator: linear allocation per frame; reset at BeginFrame.
 */
class VkStagingRing {
public:
    VkStagingRing() = default;

    /**
     * @brief Creates the ring of staging buffers (each capacityPerFrameBytes, host-visible).
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice, VkDevice device,
                            VkDeviceSize capacityPerFrameBytes, uint32_t ringSize = 2u);

    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_device != VK_NULL_HANDLE; }

    /**
     * @brief Resets the linear allocator for the given frame slot (call at start of frame).
     */
    void BeginFrame(uint32_t frameIndex) noexcept;

    /**
     * @brief Allocates a region from the staging buffer for this frame. Returns (buffer, offset) or (VK_NULL_HANDLE, 0) if over budget.
     */
    [[nodiscard]] std::pair<VkBuffer, VkDeviceSize> Allocate(uint32_t frameIndex, VkDeviceSize sizeInBytes) noexcept;

    /**
     * @brief Returns mapped CPU pointer for the given frame's buffer (for copying source data).
     */
    [[nodiscard]] void* GetMappedPointer(uint32_t frameIndex) const noexcept;

    /** @brief Capacity per frame in bytes. */
    [[nodiscard]] VkDeviceSize CapacityPerFrame() const noexcept { return m_capacityPerFrame; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDeviceSize m_capacityPerFrame = 0;
    uint32_t m_ringSize = 0;
    std::vector<VkBuffer> m_buffers;
    std::vector<VkDeviceMemory> m_memories;
    std::vector<void*> m_mapped;
    std::vector<VkDeviceSize> m_used;
};

/**
 * @brief Upload queue with per-frame budget; schedules uploads (terrain/HLOD first, then texture mips).
 */
class VkUploadBudget {
public:
    /** Priority: 0 = terrain/HLOD (first), 1 = texture mips (then). */
    static constexpr int kPriorityTerrainHlod = 0;
    static constexpr int kPriorityTextureMip = 1;

    using RecordCopyFn = std::function<void(VkCommandBuffer cmd, VkBuffer stagingBuffer, VkDeviceSize offset)>;

    VkUploadBudget() = default;

    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice, VkDevice device,
                            VkDeviceSize budgetBytesPerFrame = 32u * 1024u * 1024u);

    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_staging.IsValid(); }

    /**
     * @brief Resets staging allocator for this frame (call at start of frame).
     */
    void BeginFrame(uint32_t frameIndex) noexcept;

    /**
     * @brief Submits an upload. priority: kPriorityTerrainHlod first, kPriorityTextureMip then.
     */
    void SubmitUpload(int priority, VkDeviceSize sizeInBytes, RecordCopyFn recordCopy);

    /**
     * @brief Processes pending uploads up to budget, in priority order. Call after BeginFrame.
     */
    void ProcessFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /**
     * @brief Returns bytes used this frame (after ProcessFrame).
     */
    [[nodiscard]] VkDeviceSize GetBudgetUsedThisFrame() const noexcept { return m_usedThisFrame; }

    /** @brief Budget in bytes per frame (configurable). */
    [[nodiscard]] VkDeviceSize BudgetPerFrame() const noexcept { return m_budgetPerFrame; }

private:
    struct PendingUpload {
        int priority = 0;
        VkDeviceSize size = 0;
        RecordCopyFn recordCopy;
    };
    VkStagingRing m_staging;
    VkDeviceSize m_budgetPerFrame = 0;
    std::vector<PendingUpload> m_pending;
    VkDeviceSize m_usedThisFrame = 0;
};

} // namespace engine::render::vk
