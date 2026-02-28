#pragma once

/**
 * @file VkExposureReduce.h
 * @brief Luminance reduction (log-average) + temporal exposure adaptation (M08.3).
 *
 * Compute pass reduces HDR luminance to one value, then exposure = lerp(prev, target, 1-exp(-dt*speed)).
 * key=0.18, targetExposure = key / logAvgLuminance.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Luminance reduction (2 compute passes) + exposure buffer (ping-pong).
 *
 * Pass 1: sample HDR, output (logSum, count) per workgroup.
 * Pass 2: reduce to one (logSum, count), compute newExposure = lerp(prev, target, 1-exp(-dt*speed)), write exposure buffer.
 */
class VkExposureReduce {
public:
    VkExposureReduce() = default;

    ~VkExposureReduce();

    VkExposureReduce(const VkExposureReduce&) = delete;
    VkExposureReduce& operator=(const VkExposureReduce&) = delete;

    /**
     * @brief Creates reduction buffer, 2 exposure buffers, and 3 compute pipelines.
     *
     * @param physicalDevice  Physical device.
     * @param device          Logical device.
     * @param maxPixels       Max pixel count (width*height) for reduction sizing.
     * @param compReduce      SPIR-V for first reduce pass.
     * @param compMid         SPIR-V for mid reduce pass.
     * @param compFinal       SPIR-V for final reduce + exposure adapt.
     * @return                true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            uint32_t         maxPixels,
                            const std::vector<uint8_t>& compReduce,
                            const std::vector<uint8_t>& compMid,
                            const std::vector<uint8_t>& compFinal);

    /**
     * @brief Recreates buffers if maxPixels changed (e.g. resize).
     */
    [[nodiscard]] bool Recreate(uint32_t maxPixels);

    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_reducePipeline != VK_NULL_HANDLE; }

    /** @brief Bind HDR image view for the reduce pass (call before Dispatch). */
    void SetHDRView(VkImageView hdrView);

    /**
     * @brief Record reduce dispatches and exposure update. Call after binding HDR view.
     *
     * @param cmd       Command buffer.
     * @param width     HDR image width.
     * @param height    HDR image height.
     * @param dt        Frame delta time (seconds).
     * @param key       Middle-gray key (default 0.18).
     * @param speed     Adaptation speed.
     */
    void Dispatch(VkCommandBuffer cmd, uint32_t width, uint32_t height, float dt, float key, float speed);

    /** @brief Index of exposure buffer to bind to tonemap (swap each frame after Dispatch). */
    [[nodiscard]] uint32_t GetExposureBufferIndex() const noexcept { return m_exposureWriteIndex; }

    /** @brief Exposure buffer (UBO) for current frame. Bind to tonemap. */
    [[nodiscard]] VkBuffer GetExposureBuffer() const noexcept;
    [[nodiscard]] VkDeviceSize GetExposureBufferOffset() const noexcept { return 0; }
    [[nodiscard]] VkDeviceSize GetExposureBufferSize() const noexcept { return 4; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    uint32_t         m_maxPixels       = 0u;

    VkBuffer         m_reductionBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   m_reductionMemory = VK_NULL_HANDLE;
    VkDeviceSize     m_reductionSize   = 0u;

    VkBuffer         m_exposureBuffers[2]{};
    VkDeviceMemory   m_exposureMemory[2]{};

    VkDescriptorSetLayout m_reduceSetLayout   = VK_NULL_HANDLE;
    VkPipelineLayout     m_reduceLayout      = VK_NULL_HANDLE;
    VkPipeline           m_reducePipeline    = VK_NULL_HANDLE;
    VkDescriptorPool     m_reducePool        = VK_NULL_HANDLE;
    VkDescriptorSet       m_reduceSet         = VK_NULL_HANDLE;
    VkSampler            m_reduceSampler     = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_midSetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout     m_midLayout        = VK_NULL_HANDLE;
    VkPipeline           m_midPipeline      = VK_NULL_HANDLE;
    VkDescriptorPool     m_midPool          = VK_NULL_HANDLE;
    VkDescriptorSet       m_midSet           = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_finalSetLayout   = VK_NULL_HANDLE;
    VkPipelineLayout     m_finalLayout      = VK_NULL_HANDLE;
    VkPipeline           m_finalPipeline    = VK_NULL_HANDLE;
    VkDescriptorPool     m_finalPool         = VK_NULL_HANDLE;
    VkDescriptorSet       m_finalSet         = VK_NULL_HANDLE;

    uint32_t m_exposureWriteIndex = 0u;
    VkDeviceSize m_level0Offset   = 0u;
    VkDeviceSize m_level1Size     = 0u;
    uint32_t m_numGroupsLevel0    = 0u;
    uint32_t m_numGroupsLevel1   = 0u;
};

} // namespace engine::render::vk
