#pragma once

/**
 * @file VkSsaoPipeline.h
 * @brief Fullscreen SSAO generate pass: depth + normal -> occlusion 0..1.
 *
 * Ticket: M06.2 — SSAO: generate pass (depth+normal).
 */

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render::vk {

/**
 * @brief SSAO generate pipeline: samples depth + normal (GBuffer), kernel UBO, noise texture; writes occlusion.
 */
class VkSsaoPipeline {
public:
    VkSsaoPipeline() = default;

    ~VkSsaoPipeline();

    VkSsaoPipeline(const VkSsaoPipeline&) = delete;
    VkSsaoPipeline& operator=(const VkSsaoPipeline&) = delete;

    /**
     * @brief Creates pipeline, descriptor set (depth, normal, kernel UBO, noise), push constants (invProj, proj, view).
     */
    [[nodiscard]] bool Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    /**
     * @brief Binds depth view, normal view, kernel buffer, noise view+sampler to the descriptor set.
     */
    void SetInputs(VkImageView depthView,
                   VkImageView normalView,
                   VkBuffer kernelBuffer,
                   VkDeviceSize kernelBufferSize,
                   VkImageView noiseView,
                   VkSampler noiseSampler);

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
    VkSampler              m_sampler        = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
