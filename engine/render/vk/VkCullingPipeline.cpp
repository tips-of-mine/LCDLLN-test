/**
 * @file VkCullingPipeline.cpp
 * @brief Compute frustum culling pipeline (M18.2).
 */

#include "engine/render/vk/VkCullingPipeline.h"
#include "engine/core/Log.h"

#include <array>
#include <cstring>
#include <vector>

namespace engine::render::vk {

namespace {

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint8_t>& spirv) {
    if (spirv.empty() || (spirv.size() % 4) != 0)
        return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(spirv.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return mod;
}

} // namespace

bool VkCullingPipeline::Init(VkDevice device,
                             const std::vector<uint8_t>& compSpirv,
                             VkBuffer drawItemBuf,
                             VkBuffer indirectBuf,
                             VkBuffer countBuf,
                             VkBuffer visibleBuf) {
    if (device == VK_NULL_HANDLE || compSpirv.empty() ||
        drawItemBuf == VK_NULL_HANDLE || indirectBuf == VK_NULL_HANDLE ||
        countBuf == VK_NULL_HANDLE || visibleBuf == VK_NULL_HANDLE) {
        return false;
    }
    m_device = device;

    VkShaderModule compModule = CreateShaderModule(device, compSpirv);
    if (compModule == VK_NULL_HANDLE) {
        LOG_ERROR(Render, "VkCullingPipeline: compute shader module failed");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount  = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount  = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount  = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount  = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 4;
    dslci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_setLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, compModule, nullptr);
        LOG_ERROR(Render, "VkCullingPipeline: descriptor set layout failed");
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset      = 0;
    pushRange.size        = 128; // 6*vec4 + uint + padding

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &m_setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, compModule, nullptr);
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkCullingPipeline: pipeline layout failed");
        return false;
    }

    VkComputePipelineCreateInfo cpci{};
    cpci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = compModule;
    cpci.stage.pName  = "main";
    cpci.layout       = m_layout;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, compModule, nullptr);
        m_layout  = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkCullingPipeline: compute pipeline failed");
        return false;
    }
    vkDestroyShaderModule(device, compModule, nullptr);

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 4;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets        = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = poolSizes.data();
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_pool) != VK_SUCCESS) {
        Shutdown();
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool     = m_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &m_setLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_descriptorSet) != VK_SUCCESS) {
        Shutdown();
        return false;
    }

    std::array<VkDescriptorBufferInfo, 4> dbInfos{};
    dbInfos[0].buffer = drawItemBuf;
    dbInfos[0].offset = 0;
    dbInfos[0].range  = VK_WHOLE_SIZE;
    dbInfos[1].buffer = indirectBuf;
    dbInfos[1].offset = 0;
    dbInfos[1].range  = VK_WHOLE_SIZE;
    dbInfos[2].buffer = countBuf;
    dbInfos[2].offset = 0;
    dbInfos[2].range  = VK_WHOLE_SIZE;
    dbInfos[3].buffer = visibleBuf;
    dbInfos[3].offset = 0;
    dbInfos[3].range  = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 4> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet           = m_descriptorSet;
        writes[i].dstBinding       = i;
        writes[i].dstArrayElement  = 0;
        writes[i].descriptorCount  = 1;
        writes[i].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo      = &dbInfos[i];
    }
    vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);

    return true;
}

void VkCullingPipeline::Shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        m_pool          = VK_NULL_HANDLE;
        m_descriptorSet = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

void VkCullingPipeline::Dispatch(VkCommandBuffer cmd,
                                 const float frustumPlanes[24],
                                 uint32_t drawItemCount) const {
    if (m_pipeline == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE || drawItemCount == 0u)
        return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_descriptorSet, 0, nullptr);
    struct Push {
        float planes[24];
        uint32_t drawItemCount;
        uint32_t _pad[3];
    } push;
    std::memcpy(push.planes, frustumPlanes, sizeof(push.planes));
    push.drawItemCount = drawItemCount;
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, &push);
    const uint32_t groupCount = (drawItemCount + 63u) / 64u;
    vkCmdDispatch(cmd, groupCount, 1, 1);
}

} // namespace engine::render::vk
