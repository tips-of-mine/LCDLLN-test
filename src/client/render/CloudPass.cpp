#include "src/client/render/CloudPass.h"
#include "src/client/render/PipelineCache.h"
#include "src/shared/core/Log.h"

#include <array>
#include <cstring>

namespace engine::render
{
	namespace
	{
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t wordCount)
		{
			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode    = code;
			VkShaderModule mod = VK_NULL_HANDLE;
			vkCreateShaderModule(device, &info, nullptr, &mod);
			return mod;
		}
	}

	bool CloudPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxFrames, VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
			|| vertWordCount == 0 || fragWordCount == 0)
		{
			LOG_ERROR(Render, "CloudPass::Init: invalid arguments");
			return false;
		}
		m_maxFrames = maxFrames > 0 ? maxFrames : 1;

		// 1. Render pass : 1 color attachment.
		{
			VkAttachmentDescription colorAtt{};
			colorAtt.format         = sceneColorHDRFormat;
			colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
			colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments    = &colorRef;

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
				| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			VkRenderPassCreateInfo rpInfo{};
			rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.attachmentCount = 1;
			rpInfo.pAttachments    = &colorAtt;
			rpInfo.subpassCount    = 1;
			rpInfo.pSubpasses      = &subpass;
			rpInfo.dependencyCount = 1;
			rpInfo.pDependencies   = &dep;

			if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateRenderPass failed");
				return false;
			}
		}

		// 2. Descriptor set layout : 2 combined image samplers (scene color, depth).
		{
			std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
			for (size_t i = 0; i < bindings.size(); ++i)
			{
				bindings[i].binding         = static_cast<uint32_t>(i);
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			layoutInfo.pBindings    = bindings.data();
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateDescriptorSetLayout failed");
				Destroy(device); return false;
			}
		}

		// 3. Descriptor pool.
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 2 * m_maxFrames;
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = m_maxFrames;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateDescriptorPool failed");
				Destroy(device); return false;
			}
		}

		// 4. Alloue un descriptor set par frame.
		{
			std::vector<VkDescriptorSetLayout> layouts(m_maxFrames, m_descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool     = m_descriptorPool;
			allocInfo.descriptorSetCount = m_maxFrames;
			allocInfo.pSetLayouts        = layouts.data();
			m_descriptorSets.resize(m_maxFrames, VK_NULL_HANDLE);
			if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkAllocateDescriptorSets failed");
				Destroy(device); return false;
			}
		}

		// 5. Samplers : linéaire clamp (scene), nearest clamp (depth).
		{
			VkSamplerCreateInfo si{};
			si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter    = VK_FILTER_LINEAR;
			si.minFilter    = VK_FILTER_LINEAR;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.maxLod       = 0.0f;
			if (vkCreateSampler(device, &si, nullptr, &m_linearSampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateSampler (linear) failed");
				Destroy(device); return false;
			}
			si.magFilter = VK_FILTER_NEAREST;
			si.minFilter = VK_FILTER_NEAREST;
			if (vkCreateSampler(device, &si, nullptr, &m_nearestSampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateSampler (nearest) failed");
				Destroy(device); return false;
			}
		}

		// 6. Pipeline layout : set 0 + push constants (176 o, fragment).
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pushRange.offset     = 0;
			pushRange.size       = static_cast<uint32_t>(sizeof(CloudPushConstants));
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount         = 1;
			layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushRange;
			if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreatePipelineLayout failed");
				Destroy(device); return false;
			}
		}

		// 7. Pipeline graphique fullscreen triangle (pas de vertex input, pas de depth test).
		{
			VkShaderModule vertMod = CreateShaderModule(device, vertSpirv, vertWordCount);
			VkShaderModule fragMod = CreateShaderModule(device, fragSpirv, fragWordCount);
			if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "CloudPass: shader module creation failed");
				if (vertMod) vkDestroyShaderModule(device, vertMod, nullptr);
				if (fragMod) vkDestroyShaderModule(device, fragMod, nullptr);
				Destroy(device); return false;
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertMod; stages[0].pName = "main";
			stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragMod; stages[1].pName = "main";

			VkPipelineVertexInputStateCreateInfo vi{};
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkPipelineViewportStateCreateInfo vp{};
			vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1; vp.scissorCount = 1;
			VkPipelineRasterizationStateCreateInfo rs{};
			rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode    = VK_CULL_MODE_NONE; // fullscreen : pas de culling (CLAUDE.md OK)
			rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth   = 1.0f;
			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			VkPipelineDepthStencilStateCreateInfo dss{};
			dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			dss.depthTestEnable = VK_FALSE; dss.depthWriteEnable = VK_FALSE;
			VkPipelineColorBlendAttachmentState blendAtt{};
			blendAtt.blendEnable    = VK_FALSE;
			blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
				| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo cb{};
			cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1; cb.pAttachments = &blendAtt;
			VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

			VkGraphicsPipelineCreateInfo gpInfo{};
			gpInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			gpInfo.stageCount          = 2;
			gpInfo.pStages             = stages;
			gpInfo.pVertexInputState   = &vi;
			gpInfo.pInputAssemblyState = &ia;
			gpInfo.pViewportState      = &vp;
			gpInfo.pRasterizationState = &rs;
			gpInfo.pMultisampleState   = &ms;
			gpInfo.pDepthStencilState  = &dss;
			gpInfo.pColorBlendState    = &cb;
			gpInfo.pDynamicState       = &dyn;
			gpInfo.layout              = m_pipelineLayout;
			gpInfo.renderPass          = m_renderPass;
			gpInfo.subpass             = 0;

			AssertPipelineCreationAllowed();
			PipelineCache::RegisterWarmupKey(HashGraphicsPsoKey(m_renderPass, 0, m_pipelineLayout, sceneColorHDRFormat, VK_FORMAT_UNDEFINED));
			VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpInfo, nullptr, &m_pipeline);
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateGraphicsPipelines failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		LOG_INFO(Render, "CloudPass: initialized (maxFrames={})", m_maxFrames);
		return true;
	}

	void CloudPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent, ResourceId idSceneColorIn, ResourceId idDepth,
		ResourceId idSceneColorOut, const CloudPushConstants& params, uint32_t frameIndex)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0) return;

		VkImageView viewSceneIn = registry.getImageView(idSceneColorIn);
		VkImageView viewDepth   = registry.getImageView(idDepth);
		VkImageView viewOut     = registry.getImageView(idSceneColorOut);
		if (viewSceneIn == VK_NULL_HANDLE || viewDepth == VK_NULL_HANDLE || viewOut == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "CloudPass::Record: missing image views, skipping");
			return;
		}

		const uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		std::array<VkDescriptorImageInfo, 2> imageInfos{};
		imageInfos[0] = { m_linearSampler,  viewSceneIn, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_nearestSampler, viewDepth,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		std::array<VkWriteDescriptorSet, 2> writes{};
		for (size_t i = 0; i < writes.size(); ++i)
		{
			writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet          = ds;
			writes[i].dstBinding      = static_cast<uint32_t>(i);
			writes[i].descriptorCount = 1;
			writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo      = &imageInfos[i];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		// Audit 2026-06-10 (Lot B2) — cache framebuffer (pattern WaterPass) :
		// l'ancien framebuffer temporaire était détruit avant le vkQueueSubmit (UB).
		FramebufferKey fbKey{ viewOut, extent.width, extent.height };
		VkFramebuffer fb = VK_NULL_HANDLE;
		auto fbIt = m_framebufferCache.find(fbKey);
		if (fbIt != m_framebufferCache.end())
		{
			fb = fbIt->second;
		}
		else
		{
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = m_renderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &viewOut;
			fbInfo.width           = extent.width;
			fbInfo.height          = extent.height;
			fbInfo.layers          = 1;
			if (vkCreateFramebuffer(device, &fbInfo, nullptr, &fb) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass::Record: vkCreateFramebuffer failed");
				return;
			}
			m_framebufferCache[fbKey] = fb;
		}

		VkClearValue clearVal{};
		clearVal.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass      = m_renderPass;
		rpBegin.framebuffer     = fb;
		rpBegin.renderArea      = { { 0, 0 }, extent };
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues    = &clearVal;
		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &ds, 0, nullptr);

		VkViewport viewport{};
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			0, static_cast<uint32_t>(sizeof(CloudPushConstants)), &params);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd);
	}

	// Audit 2026-06-10 (Lot B2) — détruit les framebuffers cachés (pattern WaterPass).
	void CloudPass::InvalidateFramebufferCache(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;

		for (auto& kv : m_framebufferCache)
		{
			if (kv.second != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device, kv.second, nullptr);
		}
		m_framebufferCache.clear();
	}

	void CloudPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) { LOG_INFO(Render, "[CloudPass] Destroyed"); return; }
		// Lot B2 : vider le cache de framebuffers avant de détruire le render pass.
		InvalidateFramebufferCache(device);
		if (m_pipeline)            { vkDestroyPipeline(device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
		if (m_pipelineLayout)      { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
		if (m_nearestSampler)      { vkDestroySampler(device, m_nearestSampler, nullptr); m_nearestSampler = VK_NULL_HANDLE; }
		if (m_linearSampler)       { vkDestroySampler(device, m_linearSampler, nullptr); m_linearSampler = VK_NULL_HANDLE; }
		m_descriptorSets.clear();
		if (m_descriptorPool)      { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
		if (m_descriptorSetLayout) { vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
		if (m_renderPass)          { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
		LOG_INFO(Render, "[CloudPass] Destroyed");
	}
}
