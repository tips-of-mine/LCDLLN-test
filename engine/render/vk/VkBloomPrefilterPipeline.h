#pragma once

/**
 * @file VkBloomPrefilterPipeline.h
 * @brief Fullscreen bloom prefilter: sample HDR, soft threshold + knee, output to Mip0.
 *
 * Ticket: M08.1 — Bloom: prefilter + downsample pyramid.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Prefilter pass: samples HDR, applies threshold + knee, writes single color (BloomMip0).
 */
class VkBloomPrefilterPipeline {
public:
    VkBloomPrefilterPipeline() = default;

    ~VkBloomPrefilterPipeline();

    VkBloomPrefilterPipeline(const VkBloomPrefilterPipeline&) = delete;
    VkBloomPrefilterPipeline& operator=(const VkBloomPrefilterPipeline&) = delete;

    /**
     * @brief Creates pipeline and descriptor set (1 binding: HDR texture).
     */
    [[nodiscard]] bool Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    void SetHDRView(VkImageView hdrView);
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
