#pragma once

/**
 * @file VkBloomUpsamplePipeline.h
 * @brief Fullscreen bloom upsample: sample smaller mip, bilinear, additive blend to larger mip.
 *
 * Ticket: M08.2 — Bloom: upsample + combine HDR.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Upsample pass: samples one texture (smaller mip), writes with additive blend to target.
 */
class VkBloomUpsamplePipeline {
public:
    VkBloomUpsamplePipeline() = default;

    ~VkBloomUpsamplePipeline();

    VkBloomUpsamplePipeline(const VkBloomUpsamplePipeline&) = delete;
    VkBloomUpsamplePipeline& operator=(const VkBloomUpsamplePipeline&) = delete;

    /**
     * @brief Creates pipeline and descriptor set (1 binding: source texture). Blend: additive (ONE, ONE).
     */
    [[nodiscard]] bool Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    void SetSourceView(VkImageView sourceView);
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return m_layout; }
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const noexcept { return m_descriptorSet; }

private:
    VkDevice              m_device         = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_layout        = VK_NULL_HANDLE;
    VkPipeline            m_pipeline      = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
    VkSampler             m_sampler       = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
