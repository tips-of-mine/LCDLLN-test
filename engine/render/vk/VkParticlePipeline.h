#pragma once

/**
 * @file VkParticlePipeline.h
 * @brief Billboard particle pipeline: alpha blend, depth test no write. M17.2.
 */

#include <vulkan/vulkan.h>
#include <vector>

namespace engine::render::vk {

class VkParticlePipeline {
public:
    VkParticlePipeline() = default;
    ~VkParticlePipeline();

    [[nodiscard]] bool Init(VkDevice device, VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);
    void Shutdown();

    [[nodiscard]] bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_layout; }
    static constexpr uint32_t kPushConstantSize = 128;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
