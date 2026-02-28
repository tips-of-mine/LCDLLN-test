#pragma once

/**
 * @file VkMaterial.h
 * @brief Material struct: BaseColor/Normal/ORM texture handles + descriptor set.
 *
 * Ticket: M03.3 — Materials: BaseColor/Normal/ORM + loader.
 */

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::render::vk {

/**
 * @brief Material: three texture views (BaseColor sRGB, Normal linear, ORM linear) + params.
 *
 * ORM packing: R=AO, G=Roughness, B=Metallic. Binding stable: 0=BaseColor, 1=Normal, 2=ORM.
 */
struct VkMaterial {
    VkImageView baseColorView = VK_NULL_HANDLE;
    VkImageView normalView    = VK_NULL_HANDLE;
    VkImageView ormView       = VK_NULL_HANDLE;
    VkSampler   sampler       = VK_NULL_HANDLE;
    float       tiling[2]     = {1.0f, 1.0f};
    std::uint32_t flags      = 0;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

/**
 * @brief Creates the descriptor set layout for materials (3 combined image samplers).
 *
 * Bindings: 0 = BaseColor, 1 = Normal, 2 = ORM. Stable for geometry pass.
 *
 * @param device Logical device.
 * @param outLayout Created layout (caller must destroy).
 * @return true on success.
 */
[[nodiscard]] bool CreateMaterialDescriptorSetLayout(VkDevice device,
                                                     VkDescriptorSetLayout* outLayout);

/**
 * @brief Allocates and updates a descriptor set for the given material.
 *
 * @param device    Logical device.
 * @param pool      Descriptor pool (must support 3 combined image samplers).
 * @param layout    Material descriptor set layout.
 * @param material  Material with valid image views and sampler.
 * @param outSet    Allocated and written descriptor set.
 * @return true on success.
 */
[[nodiscard]] bool AllocAndUpdateMaterialDescriptorSet(
    VkDevice device,
    VkDescriptorPool pool,
    VkDescriptorSetLayout layout,
    const VkMaterial& material,
    VkDescriptorSet* outSet);

} // namespace engine::render::vk
