#pragma once

/**
 * @file VkBrdfLut.h
 * @brief BRDF LUT 2D (256x256 RG16F) for split-sum specular GGX, generated via compute.
 *
 * Ticket: M05.1 — IBL: BRDF LUT (compute) generation.
 *
 * Created at boot; image + view + sampler. After Generate(), layout is SHADER_READ_ONLY_OPTIMAL.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render::vk {

/** BRDF LUT resolution (width and height). */
constexpr uint32_t kBrdfLutSize = 256u;

/**
 * @brief BRDF LUT texture: 256x256 RG16F, compute-generated, then sampled in lighting.
 */
class VkBrdfLut {
public:
    VkBrdfLut() = default;

    ~VkBrdfLut();

    VkBrdfLut(const VkBrdfLut&) = delete;
    VkBrdfLut& operator=(const VkBrdfLut&) = delete;
    VkBrdfLut(VkBrdfLut&&) = delete;
    VkBrdfLut& operator=(VkBrdfLut&&) = delete;

    /**
     * @brief Creates image (storage + sampled), view, sampler and compute pipeline.
     *
     * @param physicalDevice Physical device.
     * @param device         Logical device.
     * @param compSpirv      Compute shader SPIR-V (GGX BRDF LUT).
     * @return               true on success.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            const std::vector<uint8_t>& compSpirv);

    /**
     * @brief Records and submits compute dispatch (256x256), then transitions image to SHADER_READ_ONLY_OPTIMAL.
     *
     * Call once at boot after Init. Uses a temporary command pool and fence.
     *
     * @param device       Logical device.
     * @param queue        Queue for submit (e.g. graphics queue; must support compute).
     * @param queueFamilyIndex Queue family index for the command pool.
     * @return             true on success.
     */
    [[nodiscard]] bool Generate(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex);

    /**
     * @brief Destroys image, view, sampler, pipeline and descriptor resources.
     */
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept { return m_image != VK_NULL_HANDLE; }

    /** @brief Image handle. */
    [[nodiscard]] VkImage GetImage() const noexcept { return m_image; }
    /** @brief Image view for sampling. */
    [[nodiscard]] VkImageView GetView() const noexcept { return m_imageView; }
    /** @brief Sampler for sampling the LUT. */
    [[nodiscard]] VkSampler GetSampler() const noexcept { return m_sampler; }
    [[nodiscard]] uint32_t Size() const noexcept { return kBrdfLutSize; }

private:
    void DestroyResources();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device        = VK_NULL_HANDLE;

    VkImage        m_image     = VK_NULL_HANDLE;
    VkDeviceMemory m_memory    = VK_NULL_HANDLE;
    VkImageView    m_imageView = VK_NULL_HANDLE;
    VkSampler      m_sampler   = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_setLayout   = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline    = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet  = VK_NULL_HANDLE;
};

} // namespace engine::render::vk
