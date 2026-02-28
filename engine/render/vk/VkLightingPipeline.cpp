/**
 * @file VkLightingPipeline.cpp
 * @brief Fullscreen lighting pass: GBuffer sampling + PBR UBO.
 */

#include "engine/render/vk/VkLightingPipeline.h"
#include "engine/render/Csm.h"
#include "engine/core/Log.h"

#include <array>
#include <cstring>
#include <vector>

namespace engine::render::vk {

namespace {

constexpr VkDeviceSize kUBOSize = 484u;  // ... + 4 (prefilteredMipCount, M05.3)

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint8_t>& spirv) {
    if (spirv.empty() || (spirv.size() % 4) != 0) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(spirv.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) return VK_NULL_HANDLE;
    return mod;
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

} // namespace

VkLightingPipeline::~VkLightingPipeline() {
    Shutdown();
}

bool VkLightingPipeline::Init(VkPhysicalDevice physicalDevice,
                              VkDevice device,
                              VkRenderPass renderPass,
                              const std::vector<uint8_t>& vertSpirv,
                              const std::vector<uint8_t>& fragSpirv) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE ||
        vertSpirv.empty() || fragSpirv.empty())
        return false;
    m_device = device;

    VkShaderModule vertModule = CreateShaderModule(device, vertSpirv);
    VkShaderModule fragModule = CreateShaderModule(device, fragSpirv);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragModule, nullptr);
        LOG_ERROR(Render, "VkLightingPipeline: failed to create shader modules");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 12> bindings{};
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[i].binding            = i;
        bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount    = 1;
        bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }
    bindings[4].binding            = 4;
    bindings[4].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount    = 1;
    bindings[4].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].pImmutableSamplers = nullptr;
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[5 + i].binding            = 5 + i;
        bindings[5 + i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[5 + i].descriptorCount    = 1;
        bindings[5 + i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[5 + i].pImmutableSamplers = nullptr;
    }
    bindings[9].binding            = 9;
    bindings[9].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[9].descriptorCount    = 1;
    bindings[9].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[9].pImmutableSamplers = nullptr;
    bindings[10].binding            = 10;
    bindings[10].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[10].descriptorCount    = 1;
    bindings[10].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[10].pImmutableSamplers = nullptr;
    bindings[11].binding            = 11;
    bindings[11].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[11].descriptorCount    = 1;
    bindings[11].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[11].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 12;
    dslci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_setLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        LOG_ERROR(Render, "VkLightingPipeline: failed to create descriptor set layout");
        return false;
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount  = 1;
    plci.pSetLayouts     = &m_setLayout;
    plci.pushConstantRangeCount = 0;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        LOG_ERROR(Render, "VkLightingPipeline: failed to create pipeline layout");
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 11;  // 4 GBuffer + 4 shadow + 1 BRDF LUT + 1 irradiance + 1 prefiltered env
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes    = poolSizes.data();
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: failed to create descriptor pool");
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool     = m_descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &m_setLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_descriptorSet) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: failed to allocate descriptor set");
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter               = VK_FILTER_LINEAR;
    sci.minFilter               = VK_FILTER_LINEAR;
    sci.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipLodBias              = 0.0f;
    sci.maxAnisotropy           = 1.0f;
    sci.minLod                  = 0.0f;
    sci.maxLod                  = 1.0f;
    if (vkCreateSampler(device, &sci, nullptr, &m_sampler) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: failed to create sampler");
        return false;
    }

    VkSamplerCreateInfo shadowSci{};
    shadowSci.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    shadowSci.magFilter                = VK_FILTER_LINEAR;
    shadowSci.minFilter                = VK_FILTER_LINEAR;
    shadowSci.mipmapMode               = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadowSci.addressModeU             = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSci.addressModeV             = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSci.addressModeW             = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSci.compareEnable            = VK_TRUE;
    shadowSci.compareOp                = VK_COMPARE_OP_LESS_OR_EQUAL;
    shadowSci.borderColor              = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(device, &shadowSci, nullptr, &m_shadowSampler) != VK_SUCCESS) {
        vkDestroySampler(device, m_sampler, nullptr);
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_sampler = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: failed to create shadow sampler");
        return false;
    }

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = kUBOSize;
    bci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &m_ubo) != VK_SUCCESS) {
        vkDestroySampler(device, m_shadowSampler, nullptr);
        vkDestroySampler(device, m_sampler, nullptr);
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_sampler = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: failed to create UBO");
        return false;
    }
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, m_ubo, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, m_ubo, nullptr);
        m_ubo = VK_NULL_HANDLE;
        vkDestroySampler(device, m_shadowSampler, nullptr);
        vkDestroySampler(device, m_sampler, nullptr);
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_sampler = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: no memory type for UBO");
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_uboMemory) != VK_SUCCESS ||
        vkBindBufferMemory(device, m_ubo, m_uboMemory, 0) != VK_SUCCESS ||
        vkMapMemory(device, m_uboMemory, 0, kUBOSize, 0, &m_uboMapped) != VK_SUCCESS) {
        if (m_uboMemory != VK_NULL_HANDLE) vkFreeMemory(device, m_uboMemory, nullptr);
        vkDestroyBuffer(device, m_ubo, nullptr);
        m_ubo = VK_NULL_HANDLE;
        m_uboMemory = VK_NULL_HANDLE;
        vkDestroySampler(device, m_shadowSampler, nullptr);
        vkDestroySampler(device, m_sampler, nullptr);
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_sampler = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: UBO bind/map failed");
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 0;
    vi.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports    = nullptr;
    vp.scissorCount  = 1;
    vp.pScissors     = nullptr;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable        = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode             = VK_POLYGON_MODE_FILL;
    rs.cullMode                = VK_CULL_MODE_NONE;
    rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthBiasEnable         = VK_FALSE;
    rs.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAtt.blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.logicOpEnable   = VK_FALSE;
    cb.attachmentCount = 1;
    cb.pAttachments    = &blendAtt;

    std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynamicStates.data();

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount          = 2;
    gpci.pStages             = stages.data();
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pDepthStencilState  = nullptr;
    gpci.pColorBlendState    = &cb;
    gpci.pDynamicState       = &dyn;
    gpci.layout              = m_layout;
    gpci.renderPass          = renderPass;
    gpci.subpass             = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_pipeline) != VK_SUCCESS) {
        vkUnmapMemory(device, m_uboMemory);
        vkFreeMemory(device, m_uboMemory, nullptr);
        vkDestroyBuffer(device, m_ubo, nullptr);
        vkDestroySampler(device, m_shadowSampler, nullptr);
        vkDestroySampler(device, m_sampler, nullptr);
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_ubo = VK_NULL_HANDLE;
        m_uboMemory = VK_NULL_HANDLE;
        m_uboMapped = nullptr;
        m_sampler = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkLightingPipeline: failed to create graphics pipeline");
        return false;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    return true;
}

void VkLightingPipeline::SetGBufferViews(VkImageView viewA, VkImageView viewB, VkImageView viewC, VkImageView viewDepth) {
    if (m_device == VK_NULL_HANDLE || m_sampler == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;
    std::array<VkDescriptorImageInfo, 9> imageInfos{};
    imageInfos[0].sampler     = m_sampler;
    imageInfos[0].imageView   = viewA;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler     = m_sampler;
    imageInfos[1].imageView   = viewB;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].sampler     = m_sampler;
    imageInfos[2].imageView   = viewC;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[3].sampler     = m_sampler;
    imageInfos[3].imageView   = viewDepth;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    for (uint32_t i = 0; i < 4; ++i) {
        imageInfos[4 + i].sampler     = m_shadowSampler;
        imageInfos[4 + i].imageView   = viewDepth;
        imageInfos[4 + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    imageInfos[8].sampler     = m_sampler;
    imageInfos[8].imageView   = viewDepth;
    imageInfos[8].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 11> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet           = m_descriptorSet;
        writes[i].dstBinding       = i;
        writes[i].dstArrayElement  = 0;
        writes[i].descriptorCount  = 1;
        writes[i].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo       = &imageInfos[i];
    }
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_ubo;
    bufferInfo.offset = 0;
    bufferInfo.range  = kUBOSize;
    writes[4].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet           = m_descriptorSet;
    writes[4].dstBinding       = 4;
    writes[4].dstArrayElement  = 0;
    writes[4].descriptorCount  = 1;
    writes[4].descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[4].pBufferInfo      = &bufferInfo;
    for (uint32_t i = 0; i < 4; ++i) {
        writes[5 + i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5 + i].dstSet           = m_descriptorSet;
        writes[5 + i].dstBinding       = 5 + i;
        writes[5 + i].dstArrayElement  = 0;
        writes[5 + i].descriptorCount  = 1;
        writes[5 + i].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5 + i].pImageInfo       = &imageInfos[4 + i];
    }
    writes[9].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[9].dstSet           = m_descriptorSet;
    writes[9].dstBinding       = 9;
    writes[9].dstArrayElement  = 0;
    writes[9].descriptorCount  = 1;
    writes[9].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[9].pImageInfo       = &imageInfos[8];
    vkUpdateDescriptorSets(m_device, 10, writes.data(), 0, nullptr);
}

void VkLightingPipeline::SetIrradianceView(VkImageView view, VkSampler sampler) {
    if (m_device == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = sampler;
    imageInfo.imageView   = view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet           = m_descriptorSet;
    write.dstBinding       = 10;
    write.dstArrayElement  = 0;
    write.descriptorCount  = 1;
    write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo       = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VkLightingPipeline::SetBrdfLutView(VkImageView view, VkSampler sampler) {
    if (m_device == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = sampler;
    imageInfo.imageView   = view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet           = m_descriptorSet;
    write.dstBinding       = 9;
    write.dstArrayElement  = 0;
    write.descriptorCount  = 1;
    write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo       = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VkLightingPipeline::SetShadowMapViews(VkImageView view0, VkImageView view1, VkImageView view2, VkImageView view3) {
    if (m_device == VK_NULL_HANDLE || m_shadowSampler == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;
    std::array<VkDescriptorImageInfo, 4> imageInfos{};
    imageInfos[0].sampler     = m_shadowSampler;
    imageInfos[0].imageView   = view0;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler     = m_shadowSampler;
    imageInfos[1].imageView   = view1;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].sampler     = m_shadowSampler;
    imageInfos[2].imageView   = view2;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[3].sampler     = m_shadowSampler;
    imageInfos[3].imageView   = view3;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet           = m_descriptorSet;
        writes[i].dstBinding       = 5 + i;
        writes[i].dstArrayElement  = 0;
        writes[i].descriptorCount  = 1;
        writes[i].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &imageInfos[i];
    }
    vkUpdateDescriptorSets(m_device, 4, writes.data(), 0, nullptr);
}

void VkLightingPipeline::UpdateUniforms(const float invViewProj[16],
                                        const float view[16],
                                        const float cameraPos[3],
                                        const float lightDir[3],
                                        const float lightColor[3],
                                        const float ambient[3],
                                        const CsmUniform* csmUniform,
                                        float shadowBiasConst,
                                        float shadowBiasSlope,
                                        uint32_t shadowMapSize,
                                        uint32_t prefilteredMipCount) {
    if (!m_uboMapped) return;
    char* base = static_cast<char*>(m_uboMapped);
    std::memcpy(base, invViewProj, 64);
    std::memcpy(base + 64, cameraPos, 12);
    std::memcpy(base + 80, lightDir, 12);
    std::memcpy(base + 96, lightColor, 12);
    std::memcpy(base + 112, ambient, 12);
    std::memcpy(base + 128, view, 64);
    if (csmUniform) {
        std::memcpy(base + 192, csmUniform->lightViewProj, sizeof(csmUniform->lightViewProj));
        std::memcpy(base + 448, csmUniform->splitDepths, sizeof(csmUniform->splitDepths));
        std::memcpy(base + 464, &shadowBiasConst, 4);
        std::memcpy(base + 468, &shadowBiasSlope, 4);
        const float one = 1.0f;
        std::memcpy(base + 472, &one, 4);
    } else {
        std::memset(base + 192, 0, 256);
        std::memset(base + 448, 0, 24);
        std::memcpy(base + 464, &shadowBiasConst, 4);
        std::memcpy(base + 468, &shadowBiasSlope, 4);
        const float zero = 0.0f;
        std::memcpy(base + 472, &zero, 4);
    }
    const float sizeF = (shadowMapSize > 0u) ? static_cast<float>(shadowMapSize) : 1024.0f;
    std::memcpy(base + 476, &sizeF, 4);
    const float mipCountF = static_cast<float>(prefilteredMipCount > 0u ? prefilteredMipCount : 1u);
    std::memcpy(base + 480, &mipCountF, 4);
}

void VkLightingPipeline::SetPrefilteredEnvView(VkImageView view, VkSampler sampler) {
    if (m_device == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = sampler;
    imageInfo.imageView   = view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet           = m_descriptorSet;
    write.dstBinding       = 11;
    write.dstArrayElement  = 0;
    write.descriptorCount  = 1;
    write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo       = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VkLightingPipeline::Shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_uboMapped && m_uboMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(m_device, m_uboMemory);
        m_uboMapped = nullptr;
    }
    if (m_ubo != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_ubo, nullptr);
        m_ubo = VK_NULL_HANDLE;
    }
    if (m_uboMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_uboMemory, nullptr);
        m_uboMemory = VK_NULL_HANDLE;
    }
    if (m_shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_shadowSampler, nullptr);
        m_shadowSampler = VK_NULL_HANDLE;
    }
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSet = VK_NULL_HANDLE;
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

} // namespace engine::render::vk
