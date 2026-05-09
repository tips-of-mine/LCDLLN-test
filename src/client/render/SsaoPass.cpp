#include "engine/render/SsaoPass.h"
#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>
#include <array>
#include <cstring>

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

	bool SsaoPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat outputFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxFrames,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv || vertWordCount == 0 || fragWordCount == 0)
		{
			LOG_ERROR(Render, "SsaoPass::Init: invalid arguments");
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
			LOG_ERROR(Render, "SsaoPass: vkCreateRenderPass failed");
			return false;
		}

		std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[3].binding = 3;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[3].descriptorCount = 1;
		bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 4;
		layoutInfo.pBindings = bindings.data();
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoPass: vkCreateDescriptorSetLayout failed");
			Destroy(device);
			return false;
		}

		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = 3 * m_maxFrames;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[1].descriptorCount = 1 * m_maxFrames;
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = m_maxFrames;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoPass: vkCreateDescriptorPool failed");
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
			LOG_ERROR(Render, "SsaoPass: vkAllocateDescriptorSets failed");
			Destroy(device);
			return false;
		}

		VkSamplerCreateInfo si{};
		si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		si.magFilter = VK_FILTER_NEAREST;
		si.minFilter = VK_FILTER_NEAREST;
		si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.maxLod = 0.0f;
		if (vkCreateSampler(device, &si, nullptr, &m_sampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoPass: vkCreateSampler failed");
			Destroy(device);
			return false;
		}

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(SsaoParams);
		VkPipelineLayoutCreateInfo plInfo{};
		plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plInfo.setLayoutCount = 1;
		plInfo.pSetLayouts = &m_setLayout;
		plInfo.pushConstantRangeCount = 1;
		plInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoPass: vkCreatePipelineLayout failed");
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
		if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoPass: vkCreateGraphicsPipelines failed");
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			Destroy(device);
			return false;
		}
		vkDestroyShaderModule(device, vertMod, nullptr);
		vkDestroyShaderModule(device, fragMod, nullptr);

		LOG_INFO(Render, "SsaoPass: initialised");
		return true;
	}

	void SsaoPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
		ResourceId idDepth, ResourceId idNormal, ResourceId idSsaoRaw,
		VkBuffer kernelBuffer, VkImageView noiseView, VkSampler noiseSampler,
		const SsaoParams& params, uint32_t frameIndex)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0) return;

		VkImageView viewDepth = registry.getImageView(idDepth);
		VkImageView viewNormal = registry.getImageView(idNormal);
		VkImageView viewSsao = registry.getImageView(idSsaoRaw);
		if (viewDepth == VK_NULL_HANDLE || viewNormal == VK_NULL_HANDLE || viewSsao == VK_NULL_HANDLE)
			return;

		uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		VkDescriptorImageInfo depthImg = { m_sampler, viewDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo normalImg = { m_sampler, viewNormal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorBufferInfo bufInfo{};
		bufInfo.buffer = kernelBuffer;
		bufInfo.offset = 0;
		bufInfo.range = 528;
		VkDescriptorImageInfo noiseImg = { noiseSampler, noiseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		std::array<VkWriteDescriptorSet, 4> writes{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = ds;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &depthImg;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = ds;
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo = &normalImg;
		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = ds;
		writes[2].dstBinding = 2;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[2].pBufferInfo = &bufInfo;
		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[3].dstSet = ds;
		writes[3].dstBinding = 3;
		writes[3].descriptorCount = 1;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[3].pImageInfo = &noiseImg;
		vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &viewSsao;
		fbInfo.width = extent.width;
		fbInfo.height = extent.height;
		fbInfo.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fbInfo, nullptr, &fb) != VK_SUCCESS)
			return;

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = m_renderPass;
		rpBegin.framebuffer = fb;
		rpBegin.renderArea = { { 0, 0 }, extent };
		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &ds, 0, nullptr);
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SsaoParams), &params);

		VkViewport vp{};
		vp.width = static_cast<float>(extent.width);
		vp.height = static_cast<float>(extent.height);
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &vp);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd);
		vkDestroyFramebuffer(device, fb, nullptr);
	}

	void SsaoPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
		if (m_pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
		if (m_pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
		if (m_sampler != VK_NULL_HANDLE) { vkDestroySampler(device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
		if (m_descPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_descPool, nullptr); m_descPool = VK_NULL_HANDLE; }
		if (m_setLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr); m_setLayout = VK_NULL_HANDLE; }
		if (m_renderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
		m_descriptorSets.clear();
	}
}
