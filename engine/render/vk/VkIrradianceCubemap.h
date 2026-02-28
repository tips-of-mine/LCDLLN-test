#pragma once

/**
 * @file VkIrradianceCubemap.h
 * @brief Irradiance (diffuse) cubemap: convolution of env HDR cubemap, Lambert hemisphere.
 *
 * Ticket: M05.2 — IBL: irradiance cubemap convolution.
 *
 * Target 64x64 (or 128) per face, RGBA16F. Generated once via compute; then sampled in lighting.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/** Default irradiance map size per face (64 or 128). */
constexpr uint32_t kIrradianceMapSize = 64u;

/**
 * @brief Irradiance cubemap: target image + compute convolution from env HDR cubemap.
 */
class VkIrradianceCubemap {
public:
    VkIrradianceCubemap() = default;

    ~VkIrradianceCubemap();

    VkIrradianceCubemap(const VkIrradianceCubemap&) = delete;
    VkIrradianceCubemap& operator=(const VkIrradianceCubemap&) = delete;
    VkIrradianceCubemap(VkIrradianceCubemap&&) = delete;
    VkIrradianceCubemap& operator=(VkIrradianceCubemap&&) = delete;

    /**
     * @brief Creates target cubemap image (storage + sampled), views (2D array + cube), sampler and compute pipeline.
     *
     * @param physicalDevice Physical device.
     * @param device         Logical device.
     * @param compSpirv      Compute shader SPIR-V (Lambert convolution).
     * @param size           Per-face resolution (e.g. 64 or 128).
     * @return               true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            const std::vector<uint8_t>& compSpirv,
                            uint32_t         size = kIrradianceMapSize);

    /**
     * @brief Runs convolution compute: samples source cubemap over hemisphere, writes irradiance. Transitions to SHADER_READ_ONLY.
     *
     * @param device       Logical device.
     * @param queue        Queue for submit.
     * @param queueFamilyIndex Queue family for command pool.
     * @param sourceCubeView   Env HDR cubemap image view (VK_IMAGE_VIEW_TYPE_CUBE).
     * @param sourceSampler    Sampler for sampling the source cubemap.
     * @return             true on success.
     */
    [[nodiscard]] bool Convolve(VkDevice device,
                                VkQueue queue,
                                uint32_t queueFamilyIndex,
                                VkImageView sourceCubeView,
                                VkSampler sourceSampler);

    /**
     * @brief Destroys image, views, sampler, pipeline and descriptors.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_image != VK_NULL_HANDLE; }

    /** @brief Cube image view for sampling in lighting. */
    [[nodiscard]] VkImageView GetView() const noexcept { return m_viewCube; }
    /** @brief Sampler for sampling the irradiance cubemap. */
    [[nodiscard]] VkSampler GetSampler() const noexcept { return m_sampler; }
    [[nodiscard]] uint32_t Size() const noexcept { return m_size; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    uint32_t         m_size           = 0;

    VkImage        m_image        = VK_NULL_HANDLE;
    VkDeviceMemory m_memory       = VK_NULL_HANDLE;
    VkImageView    m_viewStorage  = VK_NULL_HANDLE;
    VkImageView    m_viewCube     = VK_NULL_HANDLE;
    VkSampler      m_sampler      = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet  = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
