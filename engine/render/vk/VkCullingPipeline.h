#pragma once

/**
 * @file VkCullingPipeline.h
 * @brief Compute pipeline: frustum cull DrawItems, append visible, write indirect (M18.2).
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/**
 * @brief Compute pipeline for GPU frustum culling.
 *
 * Binds: DrawItems (SSBO), IndirectCommands (SSBO), Count (SSBO), VisibleTransforms (SSBO).
 * Push constants: 6 frustum planes (vec4 each) + drawItemCount (uint).
 */
class VkCullingPipeline {
public:
    VkCullingPipeline() = default;
    ~VkCullingPipeline() = default;

    VkCullingPipeline(const VkCullingPipeline&) = delete;
    VkCullingPipeline& operator=(const VkCullingPipeline&) = delete;

    /**
     * @brief Creates compute pipeline and descriptor set for culling.
     *
     * @param device         Logical device.
     * @param compSpirv      Compute shader SPIR-V (frustum_cull.comp).
     * @param drawItemBuf    Buffer of DrawItemGpu (storage).
     * @param indirectBuf    Buffer of VkDrawIndirectCommand (storage).
     * @param countBuf       Buffer of 1 uint (storage, atomic).
     * @param visibleBuf     Buffer of mat4 per visible (storage).
     * @return               true on success.
     */
    [[nodiscard]] bool Init(VkDevice device,
                            const std::vector<uint8_t>& compSpirv,
                            VkBuffer drawItemBuf,
                            VkBuffer indirectBuf,
                            VkBuffer countBuf,
                            VkBuffer visibleBuf);

    /**
     * @brief Destroys pipeline and descriptor resources.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_pipeline != VK_NULL_HANDLE; }

    /**
     * @brief Binds pipeline and descriptor set, pushes constants, dispatches.
     *
     * @param cmd           Command buffer.
     * @param frustumPlanes  6 planes (nx,ny,nz,d) each = 4 floats, 24 floats total.
     * @param drawItemCount Number of DrawItems in the buffer.
     */
    void Dispatch(VkCommandBuffer cmd,
                  const float frustumPlanes[24],
                  uint32_t drawItemCount) const;

private:
    VkDevice               m_device         = VK_NULL_HANDLE;
    VkPipeline             m_pipeline      = VK_NULL_HANDLE;
    VkPipelineLayout       m_layout        = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_setLayout     = VK_NULL_HANDLE;
    VkDescriptorPool       m_pool          = VK_NULL_HANDLE;
    VkDescriptorSet        m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
