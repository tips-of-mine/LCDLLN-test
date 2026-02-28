/**
 * @file VkShadowPipeline.cpp
 * @brief Depth-only pipeline for shadow map rendering.
 *
 * Ticket: M04.2 — Shadow pass: depth-only render per cascade.
 */

#include "engine/render/vk/VkShadowPipeline.h"
#include "engine/core/Log.h"

#include <cstring>
#include <vector>

namespace engine::render::vk {

namespace {

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint8_t>& spirv) {
    if (spirv.empty() || (spirv.size() % 4) != 0) {
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(spirv.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return mod;
}

} // namespace

VkShadowPipeline::~VkShadowPipeline() {
    Shutdown();
}

bool VkShadowPipeline::Init(VkDevice device,
                            VkRenderPass renderPass,
                            const std::vector<uint8_t>& vertSpirv,
                            const std::vector<uint8_t>& fragSpirv) {
    if (device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE ||
        vertSpirv.empty() || fragSpirv.empty()) {
        return false;
    }

    m_device = device;

    VkShaderModule vertModule = CreateShaderModule(device, vertSpirv);
    VkShaderModule fragModule = CreateShaderModule(device, fragSpirv);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragModule, nullptr);
        LOG_ERROR(Render, "VkShadowPipeline: failed to create shader modules");
        return false;
    }

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = 64; // mat4 lightViewProj

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 0;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pushConstant;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        LOG_ERROR(Render, "VkShadowPipeline: failed to create pipeline layout");
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

    std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = 12;
    attrs[2].location = 2;
    attrs[2].binding  = 0;
    attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset   = 24;

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = 32;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions     = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions   = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType                 = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology              = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia.primitiveRestartEnable = VK_FALSE;

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
    rs.cullMode                = VK_CULL_MODE_FRONT_BIT;
    rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthBiasEnable         = VK_TRUE;
    rs.depthBiasConstantFactor = 2.0f;
    rs.depthBiasSlopeFactor    = 2.0f;
    rs.depthBiasClamp          = 0.0f;
    rs.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    ms.sampleShadingEnable   = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable       = VK_TRUE;
    ds.depthWriteEnable      = VK_TRUE;
    ds.depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable     = VK_FALSE;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
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
    gpci.pDepthStencilState  = &ds;
    gpci.pColorBlendState    = nullptr;
    gpci.pDynamicState      = &dyn;
    gpci.layout             = m_layout;
    gpci.renderPass         = renderPass;
    gpci.subpass            = 0;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.logicOpEnable = VK_FALSE;
    cb.attachmentCount = 0;
    gpci.pColorBlendState = &cb;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_layout = VK_NULL_HANDLE;
        LOG_ERROR(Render, "VkShadowPipeline: failed to create graphics pipeline");
        return false;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    return true;
}

void VkShadowPipeline::Shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

} // namespace engine::render::vk
