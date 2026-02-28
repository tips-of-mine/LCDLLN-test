#pragma once

/**
 * @file VkPrefilteredEnvCubemap.h
 * @brief Prefiltered specular (GGX) cubemap with mip levels = roughness.
 *
 * Ticket: M05.3 — IBL: prefiltered specular cubemap (mips).
 *
 * Base size 256 or 512 per face, RGBA16F, mip chain. Roughness -> mip = r*(mipCount-1).
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/** Default prefiltered cubemap size per face (256 or 512). */
constexpr uint32_t kPrefilteredEnvSize = 256u;

/**
 * @brief Prefiltered specular cubemap: env HDR convolved with GGX per mip (roughness).
 */
class VkPrefilteredEnvCubemap {
public:
    VkPrefilteredEnvCubemap() = default;

    ~VkPrefilteredEnvCubemap();

    VkPrefilteredEnvCubemap(const VkPrefilteredEnvCubemap&) = delete;
    VkPrefilteredEnvCubemap& operator=(const VkPrefilteredEnvCubemap&) = delete;
    VkPrefilteredEnvCubemap(VkPrefilteredEnvCubemap&&) = delete;
    VkPrefilteredEnvCubemap& operator=(VkPrefilteredEnvCubemap&&) = delete;

    /**
     * @brief Creates cubemap image with mip chain (storage + sampled), per-mip storage views, cube view, sampler and compute pipeline.
     *
     * @param physicalDevice Physical device.
     * @param device         Logical device.
     * @param compSpirv       Compute shader SPIR-V (GGX prefilter).
     * @param size            Per-face resolution (e.g. 256 or 512).
     * @return                true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            const std::vector<uint8_t>& compSpirv,
                            uint32_t         size = kPrefilteredEnvSize);

    /**
     * @brief Runs prefilter compute per mip (roughness = mip/(mipCount-1)), then transitions image to SHADER_READ_ONLY.
     *
     * @param device       Logical device.
     * @param queue        Queue for submit.
     * @param queueFamilyIndex Queue family for command pool.
     * @param sourceCubeView   Env HDR cubemap image view.
     * @param sourceSampler    Sampler for source cubemap.
     * @return             true on success.
     */
    [[nodiscard]] bool Prefilter(VkDevice device,
                                 VkQueue queue,
                                 uint32_t queueFamilyIndex,
                                 VkImageView sourceCubeView,
                                 VkSampler sourceSampler);

    /**
     * @brief Destroys image, views, sampler, pipeline and descriptors.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_image != VK_NULL_HANDLE; }

    [[nodiscard]] VkImageView GetView() const noexcept { return m_viewCube; }
    [[nodiscard]] VkSampler GetSampler() const noexcept { return m_sampler; }
    [[nodiscard]] uint32_t Size() const noexcept { return m_size; }
    /** @brief Number of mip levels (roughness -> mip = roughness * (MipCount()-1)). */
    [[nodiscard]] uint32_t MipCount() const noexcept { return m_mipCount; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    uint32_t         m_size           = 0;
    uint32_t         m_mipCount       = 0;

    VkImage        m_image        = VK_NULL_HANDLE;
    VkDeviceMemory m_memory       = VK_NULL_HANDLE;
    /** Per-mip storage views for compute write (baseMipLevel = mip, levelCount = 1). */
    std::vector<VkImageView> m_viewStorageMips;
    VkImageView    m_viewCube     = VK_NULL_HANDLE;
    VkSampler      m_sampler      = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
    VkDescriptorPool     m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet      m_descriptorSet  = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
