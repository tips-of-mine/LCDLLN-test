#pragma once

/**
 * @file VkShadowPipeline.h
 * @brief Depth-only graphics pipeline for cascaded shadow map rendering.
 *
 * Ticket: M04.2 — Shadow pass: depth-only render per cascade.
 *
 * Uses same vertex format as geometry pass (position, normal, uv). Push constant:
 * lightViewProj (mat4). Depth bias (slope + constant) and optional front-face
 * culling to reduce shadow acne.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Depth-only pipeline for shadow map rendering.
 *
 * Vertex shader outputs gl_Position only. No color attachment; fragment shader
 * is minimal (depth is written by the pipeline).
 */
class VkShadowPipeline {
public:
    VkShadowPipeline() = default;

    ~VkShadowPipeline();

    VkShadowPipeline(const VkShadowPipeline&) = delete;
    VkShadowPipeline& operator=(const VkShadowPipeline&) = delete;
    VkShadowPipeline(VkShadowPipeline&&) = delete;
    VkShadowPipeline& operator=(VkShadowPipeline&&) = delete;

    /**
     * @brief Creates depth-only pipeline.
     *
     * @param device     Logical device.
     * @param renderPass Depth-only render pass (e.g. from VkShadowMap).
     * @param vertSpirv  Vertex shader SPIR-V (outputs position only).
     * @param fragSpirv  Fragment shader SPIR-V (can be minimal / no color output).
     * @return          true on success.
     */
    [[nodiscard]] bool Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv);

    /**
     * @brief Destroys pipeline and layout.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return m_layout; }

private:
    VkDevice          m_device   = VK_NULL_HANDLE;
    VkPipelineLayout  m_layout   = VK_NULL_HANDLE;
    VkPipeline        m_pipeline = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
