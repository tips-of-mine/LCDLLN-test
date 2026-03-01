#pragma once

/**
 * @file VkDecalPipeline.h
 * @brief Decal pipeline: cube volume, sample albedo, write GBuffer A with blend. M17.3.
 */

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render::vk {

/**
 * @brief Decal pipeline: vertex (cube in decal space), fragment (project albedo, fade).
 * Push constants: view, proj, decalPosition, decalHalfExtents, fade. Descriptor: albedo texture.
 */
class VkDecalPipeline {
public:
    VkDecalPipeline() = default;
    ~VkDecalPipeline();

    [[nodiscard]] bool Init(VkDevice device, VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);
    void Shutdown();

    /** @brief Binds albedo texture and sampler to the descriptor set. Call when texture is loaded. */
    void SetAlbedoView(VkImageView albedoView, VkSampler sampler);

    [[nodiscard]] bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_layout; }
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const { return m_descriptorSet; }

    static constexpr uint32_t kPushConstantSize = 160u;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
