#pragma once

/**
 * @file VkBloomDownsamplePipeline.h
 * @brief Fullscreen bloom downsample: sample previous mip, box/tent filter, output to next mip.
 *
 * Ticket: M08.1 — Bloom: prefilter + downsample pyramid.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Downsample pass: samples one texture (previous mip), writes to current (smaller) target.
 */
class VkBloomDownsamplePipeline {
public:
    VkBloomDownsamplePipeline() = default;

    ~VkBloomDownsamplePipeline();

    VkBloomDownsamplePipeline(const VkBloomDownsamplePipeline&) = delete;
    VkBloomDownsamplePipeline& operator=(const VkBloomDownsamplePipeline&) = delete;

    /**
     * @brief Creates pipeline and descriptor set (1 binding: source texture).
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
    VkDevice              m_device        = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_setLayout     = VK_NULL_HANDLE;
    VkPipelineLayout      m_layout        = VK_NULL_HANDLE;
    VkPipeline            m_pipeline      = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
    VkSampler             m_sampler       = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
