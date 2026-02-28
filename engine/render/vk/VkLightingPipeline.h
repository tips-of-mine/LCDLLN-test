#pragma once

/**
 * @file VkLightingPipeline.h
 * @brief Fullscreen lighting pass pipeline: reads GBuffer + Depth, PBR GGX, writes HDR.
 *
 * Ticket: M03.2 — Deferred: lighting pass fullscreen (PBR GGX).
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Lighting pass: 4 combined image samplers (GBuffer A/B/C, Depth) + UBO (light, invViewProj).
 */
class VkLightingPipeline {
public:
    VkLightingPipeline() = default;

    ~VkLightingPipeline();

    VkLightingPipeline(const VkLightingPipeline&) = delete;
    VkLightingPipeline& operator=(const VkLightingPipeline&) = delete;
    VkLightingPipeline(VkLightingPipeline&&) = delete;
    VkLightingPipeline& operator=(VkLightingPipeline&&) = delete;

    /**
     * @brief Creates pipeline, descriptor set layout, pool, set, sampler and UBO.
     *
     * @param physicalDevice Physical device (for UBO memory type).
     * @param device         Logical device.
     * @param renderPass     Render pass (e.g. VkSceneColorHDR).
     * @param vertSpirv      Vertex shader SPIR-V.
     * @param fragSpirv      Fragment shader SPIR-V.
     * @return               true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    /**
     * @brief Binds GBuffer views and sampler to the descriptor set.
     * Call after Init and when views are valid.
     */
    void SetGBufferViews(VkImageView viewA, VkImageView viewB, VkImageView viewC, VkImageView viewDepth);

    /**
     * @brief Updates UBO with lighting params, inverse view-projection and camera position.
     * Call each frame before the lighting pass.
     */
    void UpdateUniforms(const float invViewProj[16],
                       const float cameraPos[3],
                       const float lightDir[3],
                       const float lightColor[3],
                       const float ambient[3]);

    /**
     * @brief Destroys pipeline, layout, pool, set, sampler and UBO.
     */
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
    VkBuffer              m_ubo            = VK_NULL_HANDLE;
    VkDeviceMemory        m_uboMemory      = VK_NULL_HANDLE;
    void*                 m_uboMapped     = nullptr;
};

} // namespace engine::render::vk
