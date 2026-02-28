#pragma once

/**
 * @file VkGeometryPipeline.h
 * @brief Graphics pipeline for GBuffer geometry pass (VS/PS, 4 color + depth; D = velocity, M07.3).
 *
 * Ticket: M03.1 — Deferred: GBuffer resources + geometry pass.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Geometry pass pipeline: vertex + fragment shaders, 3 color outputs, depth.
 *
 * Compatible with VkGBuffer render pass (4 color: A/B/C/Velocity + depth). Vertex format: position (vec3), normal (vec3).
 */
class VkGeometryPipeline {
public:
    VkGeometryPipeline() = default;

    ~VkGeometryPipeline();

    VkGeometryPipeline(const VkGeometryPipeline&) = delete;
    VkGeometryPipeline& operator=(const VkGeometryPipeline&) = delete;
    VkGeometryPipeline(VkGeometryPipeline&&) = delete;
    VkGeometryPipeline& operator=(VkGeometryPipeline&&) = delete;

    /**
     * @brief Creates pipeline from vertex and fragment SPIR-V.
     *
     * @param device            Logical device.
     * @param renderPass        Render pass (e.g. from VkGBuffer).
     * @param vertSpirv         Vertex shader SPIR-V bytes.
     * @param fragSpirv         Fragment shader SPIR-V bytes.
     * @param materialSetLayout Optional descriptor set layout for materials (binding 0,1,2 = BaseColor, Normal, ORM).
     * @return                  true on success.
     */
    [[nodiscard]] bool Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv,
                            VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE);

    /**
     * @brief Destroys pipeline and layout.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_pipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkPipeline GetPipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return m_layout; }

private:
    VkDevice         m_device   = VK_NULL_HANDLE;
    VkPipelineLayout m_layout  = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
