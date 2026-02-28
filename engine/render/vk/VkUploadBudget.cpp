/**
 * @file VkUploadBudget.cpp
 * @brief Staging ring + upload queue with per-frame budget (M10.4).
 */

#include "engine/render/vk/VkUploadBudget.h"
#include "engine/render/vk/VkFrameResources.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cstring>

namespace engine::render::vk {

namespace {

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

} // namespace

// ---------------------------------------------------------------------------
// VkStagingRing
// ---------------------------------------------------------------------------

bool VkStagingRing::Init(VkPhysicalDevice physicalDevice, VkDevice device,
                         VkDeviceSize capacityPerFrameBytes, uint32_t ringSize) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || capacityPerFrameBytes == 0 || ringSize == 0)
        return false;
    Shutdown();
    m_device = device;
    m_capacityPerFrame = capacityPerFrameBytes;
    m_ringSize = ringSize;
    m_buffers.resize(ringSize, VK_NULL_HANDLE);
    m_memories.resize(ringSize, VK_NULL_HANDLE);
    m_mapped.resize(ringSize, nullptr);
    m_used.resize(ringSize, 0);

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = capacityPerFrameBytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    for (uint32_t i = 0; i < ringSize; ++i) {
        if (vkCreateBuffer(device, &bci, nullptr, &m_buffers[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkStagingRing: buffer creation failed");
            Shutdown();
            return false;
        }
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, m_buffers[i], &reqs);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = reqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, reqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (allocInfo.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(device, &allocInfo, nullptr, &m_memories[i]) != VK_SUCCESS ||
            vkBindBufferMemory(device, m_buffers[i], m_memories[i], 0) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkStagingRing: memory alloc/bind failed");
            Shutdown();
            return false;
        }
        if (vkMapMemory(device, m_memories[i], 0, capacityPerFrameBytes, 0, &m_mapped[i]) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkStagingRing: map failed");
            Shutdown();
            return false;
        }
    }
    return true;
}

void VkStagingRing::Shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    for (uint32_t i = 0; i < m_ringSize; ++i) {
        if (m_mapped[i]) {
            vkUnmapMemory(m_device, m_memories[i]);
            m_mapped[i] = nullptr;
        }
        if (m_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_buffers[i], nullptr);
            m_buffers[i] = VK_NULL_HANDLE;
        }
        if (m_memories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_memories[i], nullptr);
            m_memories[i] = VK_NULL_HANDLE;
        }
    }
    m_ringSize = 0;
    m_capacityPerFrame = 0;
    m_device = VK_NULL_HANDLE;
}

void VkStagingRing::BeginFrame(uint32_t frameIndex) noexcept {
    if (frameIndex < m_ringSize)
        m_used[frameIndex] = 0;
}

std::pair<VkBuffer, VkDeviceSize> VkStagingRing::Allocate(uint32_t frameIndex, VkDeviceSize sizeInBytes) noexcept {
    if (frameIndex >= m_ringSize || sizeInBytes == 0) return { VK_NULL_HANDLE, 0 };
    if (m_used[frameIndex] + sizeInBytes > m_capacityPerFrame) return { VK_NULL_HANDLE, 0 };
    VkDeviceSize offset = m_used[frameIndex];
    m_used[frameIndex] += sizeInBytes;
    return { m_buffers[frameIndex], offset };
}

void* VkStagingRing::GetMappedPointer(uint32_t frameIndex) const noexcept {
    if (frameIndex >= m_ringSize) return nullptr;
    return m_mapped[frameIndex];
}

// ---------------------------------------------------------------------------
// VkUploadBudget
// ---------------------------------------------------------------------------

bool VkUploadBudget::Init(VkPhysicalDevice physicalDevice, VkDevice device,
                          VkDeviceSize budgetBytesPerFrame) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || budgetBytesPerFrame == 0)
        return false;
    Shutdown();
    m_budgetPerFrame = budgetBytesPerFrame;
    if (!m_staging.Init(physicalDevice, device, budgetBytesPerFrame, kFramesInFlight))
        return false;
    return true;
}

void VkUploadBudget::Shutdown() {
    m_staging.Shutdown();
    m_pending.clear();
    m_budgetPerFrame = 0;
    m_usedThisFrame = 0;
}

void VkUploadBudget::BeginFrame(uint32_t frameIndex) noexcept {
    m_staging.BeginFrame(frameIndex);
    m_usedThisFrame = 0;
}

void VkUploadBudget::SubmitUpload(int priority, VkDeviceSize sizeInBytes, RecordCopyFn recordCopy) {
    if (!recordCopy || sizeInBytes == 0) return;
    m_pending.push_back({ priority, sizeInBytes, std::move(recordCopy) });
}

void VkUploadBudget::ProcessFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (cmdBuffer == VK_NULL_HANDLE || !m_staging.IsValid()) return;
    std::stable_sort(m_pending.begin(), m_pending.end(),
        [](const PendingUpload& a, const PendingUpload& b) { return a.priority < b.priority; });
    size_t write = 0;
    for (size_t i = 0; i < m_pending.size(); ++i) {
        PendingUpload& u = m_pending[i];
        if (m_usedThisFrame + u.size > m_budgetPerFrame) {
            if (write != i) m_pending[write] = std::move(u);
            ++write;
            continue;
        }
        auto [buf, offset] = m_staging.Allocate(frameIndex, u.size);
        if (buf == VK_NULL_HANDLE) {
            if (write != i) m_pending[write] = std::move(u);
            ++write;
            continue;
        }
        if (u.recordCopy) u.recordCopy(cmdBuffer, buf, offset);
        m_usedThisFrame += u.size;
    }
    m_pending.resize(write);
}

} // namespace engine::render::vk
