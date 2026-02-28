#pragma once

/**
 * @file VkTaaPipeline.h
 * @brief Fullscreen TAA pass: reproject history, clamp 3x3, blend (alpha ~0.9).
 *
 * Ticket: M07.4 — TAA pass: reprojection + clamp anti-ghost.
 *
 * Reads: SceneColor_LDR, HistoryA, HistoryB, Velocity, Depth.
 * Push constant: historyIndex (which history to sample: prev = idx^1).
 * Writes to TAA_Output (via framebuffer provided at draw).
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief TAA fullscreen pipeline: sample current + history, clamp, blend.
 */
class VkTaaPipeline {
public:
    VkTaaPipeline() = default;

    ~VkTaaPipeline();

    VkTaaPipeline(const VkTaaPipeline&) = delete;
    VkTaaPipeline& operator=(const VkTaaPipeline&) = delete;

    /**
     * @brief Creates pipeline and descriptor set (5 bindings: LDR, HistoryA, HistoryB, Velocity, Depth).
     */
    [[nodiscard]] bool Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    /**
     * @brief Updates descriptor set with current views (call each frame before draw).
     */
    void SetInputs(VkImageView ldrView,
                   VkImageView historyAView,
                   VkImageView historyBView,
                   VkImageView velocityView,
                   VkImageView depthView);

    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return m_layout; }
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const noexcept { return m_descriptorSet; }

private:
    VkDevice              m_device        = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout    = VK_NULL_HANDLE;
    VkPipelineLayout      m_layout       = VK_NULL_HANDLE;
    VkPipeline            m_pipeline     = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
    VkSampler              m_sampler      = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
