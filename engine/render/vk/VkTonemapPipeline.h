#pragma once

/**
 * @file VkTonemapPipeline.h
 * @brief Fullscreen tonemap pass: HDR->LDR filmic (ACES-ish) + gamma, writes SceneColor_LDR.
 *
 * Ticket: M03.2 — Deferred lighting. M03.4 — Tonemap filmic + exposure.
 */

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render::vk {

/**
 * @brief Tonemap pass: samples SceneColor_HDR, push constant exposure, writes LDR.
 */
class VkTonemapPipeline {
public:
    VkTonemapPipeline() = default;

    ~VkTonemapPipeline();

    VkTonemapPipeline(const VkTonemapPipeline&) = delete;
    VkTonemapPipeline& operator=(const VkTonemapPipeline&) = delete;

    /**
     * @brief Creates pipeline and descriptor set for sampling HDR texture.
     */
    [[nodiscard]] bool Init(VkDevice device, VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    /**
     * @brief Binds HDR image view to the descriptor set.
     */
    void SetHDRView(VkImageView hdrView);

    /**
     * @brief Binds exposure UBO (binding 1). Used for auto-exposure (M08.3) or manual value.
     */
    void SetExposureBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return m_layout; }
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const noexcept { return m_descriptorSet; }

private:
    VkDevice              m_device         = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_layout         = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet      m_descriptorSet  = VK_NULL_HANDLE;
    VkSampler             m_sampler        = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
