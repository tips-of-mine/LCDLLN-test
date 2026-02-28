/**
 * @file VkBloomDownsamplePipeline.cpp
 * @brief Bloom downsample: box/tent filter from previous mip to next.
 */

#include "engine/render/vk/VkBloomDownsamplePipeline.h"
#include "engine/core/Log.h"

#include <array>
#include <vector>

namespace engine::render::vk {

namespace {

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint8_t>& spirv) {
    if (spirv.empty() || (spirv.size() % 4) != 0) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(spirv.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) return VK_NULL_HANDLE;
    return mod;
}

} // namespace

VkBloomDownsamplePipeline::~VkBloomDownsamplePipeline() { Shutdown(); }

bool VkBloomDownsamplePipeline::Init(VkDevice device, VkRenderPass renderPass,
                                     const std::vector<uint8_t>& vertSpirv,
                                     const std::vector<uint8_t>& fragSpirv) {
    if (device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE || vertSpirv.empty() || fragSpirv.empty())
        return false;
    m_device = device;

    VkShaderModule vertModule = CreateShaderModule(device, vertSpirv);
    VkShaderModule fragModule = CreateShaderModule(device, fragSpirv);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragModule, nullptr);
        LOG_ERROR(Render, "VkBloomDownsamplePipeline: failed to create shader modules");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_setLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        LOG_ERROR(Render, "VkBloomDownsamplePipeline: descriptor set layout failed");
        return false;
    }

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = 8; // invSourceWidth (float), invSourceHeight (float)

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pushConstant;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkBloomDownsamplePipeline: pipeline layout failed");
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkBloomDownsamplePipeline: descriptor pool failed");
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = m_descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &m_setLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_descriptorSet) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkBloomDownsamplePipeline: allocate descriptor set failed");
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sci, nullptr, &m_sampler) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkBloomDownsamplePipeline: sampler failed");
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 0;
    vi.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = nullptr;
    vp.scissorCount = 1;
    vp.pScissors = nullptr;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthBiasEnable = VK_FALSE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.logicOpEnable = VK_FALSE;
    cb.attachmentCount = 1;
    cb.pAttachments = &blendAtt;

    std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2;
    gpci.pStages = stages.data();
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = nullptr;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyn;
    gpci.layout = m_layout;
    gpci.renderPass = renderPass;
    gpci.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_pipeline) != VK_SUCCESS) {
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
        LOG_ERROR(Render, "VkBloomDownsamplePipeline: graphics pipeline failed");
        return false;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    return true;
}

void VkBloomDownsamplePipeline::SetSourceView(VkImageView sourceView) {
    if (m_device == VK_NULL_HANDLE || m_sampler == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_sampler;
    imageInfo.imageView = sourceView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VkBloomDownsamplePipeline::Shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_sampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
    if (m_descriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
    m_descriptorSet = VK_NULL_HANDLE;
    if (m_pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    if (m_layout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, m_layout, nullptr); m_layout = VK_NULL_HANDLE; }
    if (m_setLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr); m_setLayout = VK_NULL_HANDLE; }
    m_device = VK_NULL_HANDLE;
}

} // namespace engine::render::vk
