#include "engine/render/ShadowMapPass.h"

#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

namespace engine::render
{
	namespace
	{
		constexpr uint32_t kPushConstantSize = 64u; // mat4 lightViewProj
	}

	bool ShadowMapPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat depthFormat,
		uint32_t resolution,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || vertWordCount == 0)
		{
			LOG_ERROR(Render, "ShadowMapPass::Init: invalid arguments");
			return false;
		}

		m_resolution = resolution > 0 ? resolution : 1u;

		// ---------------------------------------------------------------------
		// Render pass: single depth attachment, no colour attachments.
		// ---------------------------------------------------------------------
		VkAttachmentDescription depthAtt{};
		depthAtt.format         = depthFormat;
		depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
		depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthRef{};
		depthRef.attachment = 0;
		depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount    = 0;
		subpass.pColorAttachments       = nullptr;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dep{};
		dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass    = 0;
		dep.srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dep.dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments    = &depthAtt;
		rpInfo.subpassCount    = 1;
		rpInfo.pSubpasses      = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dep;

		if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "ShadowMapPass: vkCreateRenderPass failed");
			return false;
		}

		// ---------------------------------------------------------------------
		// Pipeline layout: push constant mat4 lightViewProj (vertex stage).
		// ---------------------------------------------------------------------
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset     = 0;
		pushRange.size       = kPushConstantSize;

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges    = &pushRange;

		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "ShadowMapPass: vkCreatePipelineLayout failed");
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
			return false;
		}

		// ---------------------------------------------------------------------
		// Shader modules.
		// ---------------------------------------------------------------------
		VkShaderModule vertModule = VK_NULL_HANDLE;
		VkShaderModule fragModule = VK_NULL_HANDLE;

		VkShaderModuleCreateInfo modInfo{};
		modInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		modInfo.pCode    = vertSpirv;
		modInfo.codeSize = vertWordCount * sizeof(uint32_t);
		if (vkCreateShaderModule(device, &modInfo, nullptr, &vertModule) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "ShadowMapPass: vertex shader module creation failed");
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
			m_renderPass     = VK_NULL_HANDLE;
			return false;
		}

		if (fragSpirv && fragWordCount > 0)
		{
			modInfo.pCode    = fragSpirv;
			modInfo.codeSize = fragWordCount * sizeof(uint32_t);
			if (vkCreateShaderModule(device, &modInfo, nullptr, &fragModule) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "ShadowMapPass: fragment shader module creation failed");
				vkDestroyShaderModule(device, vertModule, nullptr);
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				vkDestroyRenderPass(device, m_renderPass, nullptr);
				m_pipelineLayout = VK_NULL_HANDLE;
				m_renderPass     = VK_NULL_HANDLE;
				return false;
			}
		}

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName  = "main";

		uint32_t stageCount = 1;
		if (fragModule != VK_NULL_HANDLE)
		{
			stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragModule;
			stages[1].pName  = "main";
			stageCount       = 2;
		}

		// Vertex layout matches GeometryPass: position/normal/uv, but only position is used.
		VkVertexInputAttributeDescription attrs[3]{};
		attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
		attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 12;
		attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT;    attrs[2].offset = 24;

		VkVertexInputBindingDescription vbinding{};
		vbinding.binding   = 0;
		vbinding.stride    = 32;
		vbinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount   = 1;
		vi.pVertexBindingDescriptions      = &vbinding;
		vi.vertexAttributeDescriptionCount = 3;
		vi.pVertexAttributeDescriptions    = attrs;

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount  = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode             = VK_POLYGON_MODE_FILL;
		rs.cullMode                = VK_CULL_MODE_BACK_BIT; // actual mode selected at Record via dynamic state if needed.
		rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.depthClampEnable        = VK_FALSE;
		rs.depthBiasEnable         = VK_TRUE;
		rs.lineWidth               = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable  = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 0;
		cb.pAttachments    = nullptr;

		VkDynamicState dynStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};
		VkPipelineDynamicStateCreateInfo dyn{};
		dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = static_cast<uint32_t>(std::size(dynStates));
		dyn.pDynamicStates    = dynStates;

		VkGraphicsPipelineCreateInfo gpInfo{};
		gpInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gpInfo.stageCount          = stageCount;
		gpInfo.pStages             = stages;
		gpInfo.pVertexInputState   = &vi;
		gpInfo.pInputAssemblyState = &ia;
		gpInfo.pViewportState      = &vp;
		gpInfo.pRasterizationState = &rs;
		gpInfo.pMultisampleState   = &ms;
		gpInfo.pDepthStencilState  = &ds;
		gpInfo.pColorBlendState    = &cb;
		gpInfo.pDynamicState       = &dyn;
		gpInfo.layout              = m_pipelineLayout;
		gpInfo.renderPass          = m_renderPass;
		gpInfo.subpass             = 0;

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "ShadowMapPass: vkCreateGraphicsPipelines failed");
			if (fragModule != VK_NULL_HANDLE)
				vkDestroyShaderModule(device, fragModule, nullptr);
			vkDestroyShaderModule(device, vertModule, nullptr);
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
			m_renderPass     = VK_NULL_HANDLE;
			return false;
		}

		if (fragModule != VK_NULL_HANDLE)
			vkDestroyShaderModule(device, fragModule, nullptr);
		vkDestroyShaderModule(device, vertModule, nullptr);

		return true;
	}

	void ShadowMapPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		ResourceId shadowMapId,
		const float* lightViewProjMat4,
		const MeshAsset* mesh,
		float depthBiasConstant,
		float depthBiasSlope,
		bool cullFrontFaces)
	{
		(void)cullFrontFaces;
		if (!IsValid() || device == VK_NULL_HANDLE)
			return;

		VkImageView depthView = registry.getImageView(shadowMapId);
		if (depthView == VK_NULL_HANDLE)
			return;

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments    = &depthView;
		fbInfo.width           = m_resolution;
		fbInfo.height          = m_resolution;
		fbInfo.layers          = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fbInfo, nullptr, &fb) != VK_SUCCESS)
			return;

		VkClearValue clear{};
		clear.depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass      = m_renderPass;
		rpBegin.framebuffer     = fb;
		rpBegin.renderArea.offset = { 0, 0 };
		rpBegin.renderArea.extent = { m_resolution, m_resolution };
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues    = &clear;

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.width    = static_cast<float>(m_resolution);
		viewport.height   = static_cast<float>(m_resolution);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = { m_resolution, m_resolution };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

		// Depth bias and culling: bias values come from caller; cull mode is encoded in pipeline.
		if (depthBiasConstant != 0.0f || depthBiasSlope != 0.0f)
		{
			vkCmdSetDepthBias(cmd, depthBiasConstant, 0.0f, depthBiasSlope);
		}

		if (lightViewProjMat4)
		{
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
				0, kPushConstantSize, lightViewProjMat4);
		}

		if (mesh
			&& mesh->vertexBuffer != VK_NULL_HANDLE
			&& mesh->indexBuffer != VK_NULL_HANDLE
			&& mesh->indexCount > 0)
		{
			VkDeviceSize vbOffset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertexBuffer, &vbOffset);
			vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
		}

		vkCmdEndRenderPass(cmd);
		vkDestroyFramebuffer(device, fb, nullptr);
	}

	void ShadowMapPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (m_pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, m_pipeline, nullptr);
			m_pipeline = VK_NULL_HANDLE;
		}
		if (m_pipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
		}
		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}
		m_resolution = 0;
	}
}

