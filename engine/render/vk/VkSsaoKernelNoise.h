#pragma once

/**
 * @file VkSsaoKernelNoise.h
 * @brief SSAO kernel (16-32 samples) + 4x4 noise texture for TBN rotation.
 *
 * Ticket: M06.1 — SSAO: kernel + noise generation.
 *
 * Kernel biased toward center (hemisphere); noise XY random. UBO + texture uploaded at init.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/** Number of SSAO kernel samples (within 16-32). */
constexpr uint32_t kSsaoKernelSize = 32u;

/** UBO size: 32 vec3 (std140 = 32*16) + radius (4) + bias (4) + padding = 528. */
constexpr VkDeviceSize kSsaoKernelUBOSize = 528u;

/**
 * @brief Generates SSAO kernel (CPU) + 4x4 noise texture, uploads to UBO and texture.
 *
 * Kernel samples are hemisphere directions biased toward center; noise is random XY for rotation.
 * Radius and bias are clamped and stored in the UBO for use by the SSAO pass.
 */
class VkSsaoKernelNoise {
public:
    VkSsaoKernelNoise() = default;

    ~VkSsaoKernelNoise();

    VkSsaoKernelNoise(const VkSsaoKernelNoise&) = delete;
    VkSsaoKernelNoise& operator=(const VkSsaoKernelNoise&) = delete;
    VkSsaoKernelNoise(VkSsaoKernelNoise&&) = delete;
    VkSsaoKernelNoise& operator=(VkSsaoKernelNoise&&) = delete;

    /**
     * @brief Generates kernel and noise on CPU, creates UBO and 4x4 texture, uploads.
     *
     * @param physicalDevice Physical device.
     * @param device         Logical device.
     * @param queue          Queue for transfer submit.
     * @param commandPool    Command pool for one-time uploads.
     * @param radius         SSAO radius (clamped by caller, e.g. [0.01, 2.0]).
     * @param bias           SSAO bias (clamped by caller, e.g. [0.0001, 0.1]).
     * @return               true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            VkQueue          queue,
                            VkCommandPool    commandPool,
                            float            radius,
                            float            bias);

    /**
     * @brief Destroys UBO buffer, noise image/view and sampler.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_kernelBuffer != VK_NULL_HANDLE; }

    /** @brief Buffer containing kernel samples (32 vec3) + radius + bias (std140). */
    [[nodiscard]] VkBuffer GetKernelBuffer() const noexcept { return m_kernelBuffer; }
    [[nodiscard]] VkDeviceSize GetKernelBufferSize() const noexcept { return kSsaoKernelUBOSize; }
    /** @brief Noise texture 4x4 (RG8), tiled for rotation. */
    [[nodiscard]] VkImageView GetNoiseImageView() const noexcept { return m_noiseView; }
    [[nodiscard]] VkSampler GetNoiseSampler() const noexcept { return m_noiseSampler; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;

    VkBuffer       m_kernelBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_kernelMemory = VK_NULL_HANDLE;

    VkImage        m_noiseImage   = VK_NULL_HANDLE;
    VkDeviceMemory m_noiseMemory  = VK_NULL_HANDLE;
    VkImageView    m_noiseView    = VK_NULL_HANDLE;
    VkSampler      m_noiseSampler = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
