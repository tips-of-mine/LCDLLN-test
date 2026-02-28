#pragma once

/**
 * @file VkBloomCombinePipeline.h
 * @brief Fullscreen combine: SceneColor_HDR + bloom*intensity → HDR output (M08.2).
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Combine pass: samples scene HDR and bloom Mip0, outputs scene + bloom * intensity.
 */
class VkBloomCombinePipeline {
public:
    VkBloomCombinePipeline() = default;

    ~VkBloomCombinePipeline();

    VkBloomCombinePipeline(const VkBloomCombinePipeline&) = delete;
    VkBloomCombinePipeline& operator=(const VkBloomCombinePipeline&) = delete;

    /**
     * @brief Creates pipeline and descriptor set (2 bindings: scene HDR, bloom). Push: intensity (float).
     */
    [[nodiscard]] bool Init(VkDevice device,
                           VkRenderPass renderPass,
                           const std::vector<uint8_t>& vertSpirv,
                           const std::vector<uint8_t>& fragSpirv);

    void SetInputs(VkImageView sceneHdrView, VkImageView bloomView);
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return m_layout; }
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const noexcept { return m_descriptorSet; }

private:
    VkDevice              m_device         = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout     = VK_NULL_HANDLE;
    VkPipelineLayout      m_layout        = VK_NULL_HANDLE;
    VkPipeline            m_pipeline      = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
    VkSampler             m_sampler       = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
