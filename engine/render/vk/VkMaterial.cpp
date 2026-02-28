/**
 * @file VkMaterial.cpp
 * @brief Material descriptor set layout and set update.
 */

#include "engine/render/vk/VkMaterial.h"
#include "engine/core/Log.h"

#include <array>

namespace engine::render::vk {

bool CreateMaterialDescriptorSetLayout(VkDevice device,
                                       VkDescriptorSetLayout* outLayout) {
    if (device == VK_NULL_HANDLE || outLayout == nullptr) return false;
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < 3; ++i) {
        bindings[i].binding            = i;
        bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount    = 1;
        bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 3;
    ci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, outLayout) != VK_SUCCESS) {
        LOG_ERROR(Render, "CreateMaterialDescriptorSetLayout failed");
        return false;
    }
    return true;
}

bool AllocAndUpdateMaterialDescriptorSet(
    VkDevice device,
    VkDescriptorPool pool,
    VkDescriptorSetLayout layout,
    const VkMaterial& material,
    VkDescriptorSet* outSet) {
    if (device == VK_NULL_HANDLE || pool == VK_NULL_HANDLE || layout == VK_NULL_HANDLE ||
        outSet == nullptr) return false;
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;
    if (vkAllocateDescriptorSets(device, &ai, outSet) != VK_SUCCESS) {
        LOG_ERROR(Render, "AllocAndUpdateMaterialDescriptorSet: allocate failed");
        return false;
    }
    std::array<VkDescriptorImageInfo, 3> imageInfos{};
    imageInfos[0].sampler     = material.sampler;
    imageInfos[0].imageView   = material.baseColorView;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler     = material.sampler;
    imageInfos[1].imageView   = material.normalView;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].sampler     = material.sampler;
    imageInfos[2].imageView   = material.ormView;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t i = 0; i < 3; ++i) {
        writes[i].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet           = *outSet;
        writes[i].dstBinding       = i;
        writes[i].dstArrayElement  = 0;
        writes[i].descriptorCount  = 1;
        writes[i].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo       = &imageInfos[i];
    }
    vkUpdateDescriptorSets(device, 3, writes.data(), 0, nullptr);
    return true;
}

} // namespace engine::render::vk
