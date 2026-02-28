#pragma once

/**
 * @file VkSsaoBlurPipeline.h
 * @brief Fullscreen bilateral blur (H or V): SSAO + depth -> depth-aware blur.
 *
 * Ticket: M06.3 — SSAO: bilateral blur (2 passes).
 */

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render::vk {

/**
 * @brief One pipeline for horizontal or vertical blur; push constant sets direction and texel size.
 */
class VkSsaoBlurPipeline {
public:
    VkSsaoBlurPipeline() = default;

    ~VkSsaoBlurPipeline();

    VkSsaoBlurPipeline(const VkSsaoBlurPipeline&) = delete;
    VkSsaoBlurPipeline& operator=(const VkSsaoBlurPipeline&) = delete;

    /**
     * @brief Creates pipeline and descriptor set (SSAO input, depth). Push constant: direction (vec2), invSize (vec2).
     */
    [[nodiscard]] bool Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    /**
     * @brief Binds SSAO input view and depth view to the descriptor set.
     */
    void SetInputs(VkImageView ssaoView, VkImageView depthView);

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
    VkDescriptorSet       m_descriptorSet  = VK_NULL_HANDLE;
    VkSampler             m_sampler        = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
