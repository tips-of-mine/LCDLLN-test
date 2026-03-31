#include "engine/render/TaaPass.h"
#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>
#include <array>
#include <cstdio>

namespace engine::render
{
	namespace
	{
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t wordCount)
		{
			VkShaderModuleCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode = code;
			VkShaderModule mod = VK_NULL_HANDLE;
			vkCreateShaderModule(device, &info, nullptr, &mod);
			return mod;
		}
	}

	bool TaaPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat outputFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxFrames,
		VkPipelineCache pipelineCache)
	{
		LOG_INFO(Render, "[TAA] Init enter");
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv || vertWordCount == 0 || fragWordCount == 0)
		{
			LOG_ERROR(Render, "TaaPass::Init: invalid arguments");
			return false;
		}
		m_maxFrames = maxFrames > 0 ? maxFrames : 1;

		VkAttachmentDescription colorAtt{};
		colorAtt.format = outputFormat;
		colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		VkSubpassDependency dep{};
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments = &colorAtt;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;
		if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "TaaPass: vkCreateRenderPass failed");
			return false;
		}

		std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
		for (uint32_t i = 0; i < 4; ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 4;
		layoutInfo.pBindings = bindings.data();
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "TaaPass: vkCreateDescriptorSetLayout failed");
			Destroy(device);
			return false;
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 4 * m_maxFrames;
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = m_maxFrames;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "TaaPass: vkCreateDescriptorPool failed");
			Destroy(device);
			return false;
		}

		std::vector<VkDescriptorSetLayout> layouts(m_maxFrames, m_setLayout);
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descPool;
		allocInfo.descriptorSetCount = m_maxFrames;
		allocInfo.pSetLayouts = layouts.data();
		m_descriptorSets.resize(m_maxFrames, VK_NULL_HANDLE);
		if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "TaaPass: vkAllocateDescriptorSets failed");
			Destroy(device);
			return false;
		}

		VkSamplerCreateInfo si{};
		si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		si.magFilter = VK_FILTER_LINEAR;
		si.minFilter = VK_FILTER_LINEAR;
		si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.maxLod = 0.0f;
		if (vkCreateSampler(device, &si, nullptr, &m_sampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "TaaPass: vkCreateSampler failed");
			Destroy(device);
			return false;
		}

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(TaaParams);
		VkPipelineLayoutCreateInfo plInfo{};
		plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plInfo.setLayoutCount = 1;
		plInfo.pSetLayouts = &m_setLayout;
		plInfo.pushConstantRangeCount = 1;
		plInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "TaaPass: vkCreatePipelineLayout failed");
			Destroy(device);
			return false;
		}

		VkShaderModule vertMod = CreateShaderModule(device, vertSpirv, vertWordCount);
		VkShaderModule fragMod = CreateShaderModule(device, fragSpirv, fragWordCount);
		if (!vertMod || !fragMod)
		{
			if (vertMod) vkDestroyShaderModule(device, vertMod, nullptr);
			if (fragMod) vkDestroyShaderModule(device, fragMod, nullptr);
			Destroy(device);
			return false;
		}
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertMod;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragMod;
		stages[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkPipelineViewportStateCreateInfo vp{};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;
		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		VkPipelineColorBlendAttachmentState blendAtt{};
		blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &blendAtt;
		VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn{};
		dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates = dynStates;

		VkGraphicsPipelineCreateInfo gpInfo{};
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
		AssertPipelineCreationAllowed();
		PipelineCache::RegisterWarmupKey(HashGraphicsPsoKey(m_renderPass, 0, m_pipelineLayout, outputFormat, VK_FORMAT_UNDEFINED));
		VkResult result = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpInfo, nullptr, &m_pipeline);
		LOG_INFO(Render, "[TAA] vkCreateGraphicsPipelines r={}", (int)result);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "TaaPass: vkCreateGraphicsPipelines failed");
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			Destroy(device);
			return false;
		}
		vkDestroyShaderModule(device, vertMod, nullptr);
		vkDestroyShaderModule(device, fragMod, nullptr);

		LOG_INFO(Render, "TaaPass: initialised");
		return true;
	}

	void TaaPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
		ResourceId idCurrentLDR, ResourceId idHistoryPrev, ResourceId idVelocity, ResourceId idDepth,
		ResourceId idHistoryNext,
		const TaaParams& params, uint32_t frameIndex)
	{
		LOG_INFO(Render, "[TAA] Record enter frameIndex={} extent={}x{}", frameIndex, extent.width, extent.height);
		if (!IsValid() || extent.width == 0 || extent.height == 0) return;

		VkImageView viewCurrent = registry.getImageView(idCurrentLDR);
		VkImageView viewHistory = registry.getImageView(idHistoryPrev);
		VkImageView viewVel = registry.getImageView(idVelocity);
		VkImageView viewDepth = registry.getImageView(idDepth);
		VkImageView viewOut = registry.getImageView(idHistoryNext);
		LOG_INFO(Render, "[TAA] views current={} history={} vel={} depth={} out={}",
			(void*)viewCurrent, (void*)viewHistory, (void*)viewVel, (void*)viewDepth, (void*)viewOut);
		if (viewCurrent == VK_NULL_HANDLE || viewHistory == VK_NULL_HANDLE
			|| viewVel == VK_NULL_HANDLE || viewDepth == VK_NULL_HANDLE || viewOut == VK_NULL_HANDLE)
			return;

		uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		std::array<VkDescriptorImageInfo, 4> imageInfos{};
		imageInfos[0] = { m_sampler, viewCurrent, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_sampler, viewHistory, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[2] = { m_sampler, viewVel, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[3] = { m_sampler, viewDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		std::array<VkWriteDescriptorSet, 4> writes{};
		for (uint32_t i = 0; i < 4; ++i)
		{
			writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet = ds;
			writes[i].dstBinding = i;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo = &imageInfos[i];
		}
		LOG_INFO(Render, "[TAA] before vkUpdateDescriptorSets");
		vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);
		LOG_INFO(Render, "[TAA] after vkUpdateDescriptorSets");

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &viewOut;
		fbInfo.width = extent.width;
		fbInfo.height = extent.height;
		fbInfo.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fbInfo, nullptr, &fb) != VK_SUCCESS)
			return;
		LOG_INFO(Render, "[TAA] framebuffer={}", (void*)fb);

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = m_renderPass;
		rpBegin.framebuffer = fb;
		rpBegin.renderArea = { { 0, 0 }, extent };
		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		LOG_INFO(Render, "[TAA] after vkCmdBeginRenderPass");

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &ds, 0, nullptr);
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TaaParams), &params);

		VkViewport vp{};
		vp.width = static_cast<float>(extent.width);
		vp.height = static_cast<float>(extent.height);
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &vp);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdDraw(cmd, 3, 1, 0, 0);
		LOG_INFO(Render, "[TAA] after vkCmdDraw");
		vkCmdEndRenderPass(cmd);
		LOG_INFO(Render, "[TAA] after vkCmdEndRenderPass");
		vkDestroyFramebuffer(device, fb, nullptr);
		LOG_INFO(Render, "[TAA] Record done");
	}

	void TaaPass::Destroy(VkDevice device)
	{
		LOG_DEBUG(Render, "[TAA] Destroy enter");
		if (device == VK_NULL_HANDLE) return;
		if (m_pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
		if (m_pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
		if (m_sampler != VK_NULL_HANDLE) { vkDestroySampler(device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
		if (m_descPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_descPool, nullptr); m_descPool = VK_NULL_HANDLE; }
		if (m_setLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr); m_setLayout = VK_NULL_HANDLE; }
		if (m_renderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
		m_descriptorSets.clear();
		LOG_INFO(Render, "[TAA] Destroy OK");
	}
}
