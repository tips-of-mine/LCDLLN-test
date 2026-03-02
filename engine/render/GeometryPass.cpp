#include "engine/render/GeometryPass.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cstring>

namespace engine::render
{
	namespace
	{
		constexpr uint32_t kPushConstantSize = 64u; // mat4
	}

	bool GeometryPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		VkFormat formatA, VkFormat formatB, VkFormat formatC, VkFormat depthFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount)
	{
		if (!device || !vertSpirv || vertWordCount == 0 || !fragSpirv || fragWordCount == 0)
			return false;

		// Render pass: 3 color + 1 depth attachment
		VkAttachmentDescription attachments[4] = {};
		attachments[0].format = formatA;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // matches FG ColorWrite state

		attachments[1].format = formatB;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		attachments[2].format = formatC;
		attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		attachments[3].format = depthFormat;
		attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRefs[3] = {
			{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
			{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
			{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
		};
		VkAttachmentReference depthRef = { 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 3;
		subpass.pColorAttachments = colorRefs;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dep = {};
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dep.srcAccessMask = 0;
		dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo = {};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 4;
		rpInfo.pAttachments = attachments;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		VkResult result = vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "GeometryPass: vkCreateRenderPass failed: {}", static_cast<int>(result));
			return false;
		}

		// Pipeline layout: push constant mat4
		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = kPushConstantSize;

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "GeometryPass: vkCreatePipelineLayout failed: {}", static_cast<int>(result));
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
			return false;
		}

		// Shader modules
		VkShaderModule vertModule = VK_NULL_HANDLE;
		VkShaderModule fragModule = VK_NULL_HANDLE;
		VkShaderModuleCreateInfo modInfo = {};
		modInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		modInfo.pCode = vertSpirv;
		modInfo.codeSize = vertWordCount * sizeof(uint32_t);
		result = vkCreateShaderModule(device, &modInfo, nullptr, &vertModule);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "GeometryPass: vertex shader module failed: {}", static_cast<int>(result));
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
			m_renderPass = VK_NULL_HANDLE;
			return false;
		}
		modInfo.pCode = fragSpirv;
		modInfo.codeSize = fragWordCount * sizeof(uint32_t);
		result = vkCreateShaderModule(device, &modInfo, nullptr, &fragModule);
		if (result != VK_SUCCESS)
		{
			vkDestroyShaderModule(device, vertModule, nullptr);
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
			m_renderPass = VK_NULL_HANDLE;
			return false;
		}

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; // FIX: was VK_STRUCTURE_TYPE_SHADER_STAGE_CREATE_INFO
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; // FIX: was VK_STRUCTURE_TYPE_SHADER_STAGE_CREATE_INFO
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName = "main";

		// Vertex input: position(0), normal(1), uv(2), stride 32
		VkVertexInputAttributeDescription attrs[3] = {};
		attrs[0].location = 0;
		attrs[0].binding = 0;
		attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[0].offset = 0;
		attrs[1].location = 1;
		attrs[1].binding = 0;
		attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[1].offset = 12;
		attrs[2].location = 2;
		attrs[2].binding = 0;
		attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
		attrs[2].offset = 24;

		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = 32;
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = 1;
		vi.pVertexBindingDescriptions = &binding;
		vi.vertexAttributeDescriptionCount = 3;
		vi.pVertexAttributeDescriptions = attrs;

		VkPipelineInputAssemblyStateCreateInfo ia = {};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp = {};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs = {};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds = {};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		ds.depthBoundsTestEnable = VK_FALSE;
		ds.stencilTestEnable = VK_FALSE;

		VkPipelineColorBlendAttachmentState blendAtt[3] = {};
		blendAtt[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAtt[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAtt[2].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 3;
		cb.pAttachments = blendAtt;

		VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn = {};
		dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates = dynStates;

		VkGraphicsPipelineCreateInfo gpInfo = {};
		gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gpInfo.stageCount = 2;
		gpInfo.pStages = stages;
		gpInfo.pVertexInputState = &vi;
		gpInfo.pInputAssemblyState = &ia;
		gpInfo.pViewportState = &vp;
		gpInfo.pRasterizationState = &rs;
		gpInfo.pMultisampleState = &ms;
		gpInfo.pDepthStencilState = &ds;
		gpInfo.pColorBlendState = &cb;
		gpInfo.pDynamicState = &dyn;
		gpInfo.layout = m_pipelineLayout;
		gpInfo.renderPass = m_renderPass;
		gpInfo.subpass = 0;

		result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline);
		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "GeometryPass: vkCreateGraphicsPipelines failed: {}", static_cast<int>(result));
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
			m_renderPass = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	void GeometryPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
		ResourceId idA, ResourceId idB, ResourceId idC, ResourceId idDepth,
		const float* viewProjMat4, const MeshAsset* mesh)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0)
			return;

		VkImageView viewA = registry.getImageView(idA);
		VkImageView viewB = registry.getImageView(idB);
		VkImageView viewC = registry.getImageView(idC);
		VkImageView viewDepth = registry.getImageView(idDepth);
		if (viewA == VK_NULL_HANDLE || viewB == VK_NULL_HANDLE || viewC == VK_NULL_HANDLE || viewDepth == VK_NULL_HANDLE)
			return;

		VkImageView views[4] = { viewA, viewB, viewC, viewDepth };
		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = m_renderPass;
		fbInfo.attachmentCount = 4;
		fbInfo.pAttachments = views;
		fbInfo.width = extent.width;
		fbInfo.height = extent.height;
		fbInfo.layers = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;
		VkResult result = vkCreateFramebuffer(device, &fbInfo, nullptr, &fb);
		if (result != VK_SUCCESS)
			return;

		VkClearValue clearValues[4] = {};
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[3].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rpBegin = {};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = m_renderPass;
		rpBegin.framebuffer = fb;
		rpBegin.renderArea = { { 0, 0 }, extent };
		rpBegin.clearValueCount = 4;
		rpBegin.pClearValues = clearValues;

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		if (viewProjMat4)
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, kPushConstantSize, viewProjMat4);

		if (mesh && mesh->vertexBuffer != VK_NULL_HANDLE && mesh->indexBuffer != VK_NULL_HANDLE && mesh->indexCount > 0)
		{
			VkDeviceSize vbOffset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertexBuffer, &vbOffset);
			vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
		}

		vkCmdEndRenderPass(cmd);
		vkDestroyFramebuffer(device, fb, nullptr);
	}

	void GeometryPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
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
	}
}
