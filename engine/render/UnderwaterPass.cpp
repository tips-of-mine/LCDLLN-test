#include "engine/render/UnderwaterPass.h"
#include "engine/core/Log.h"

#include <cstring>
#include <vector>

namespace engine::render
{
	bool UnderwaterPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxFrames,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: device is null");
			return false;
		}
		if (!vertSpirv || vertWordCount == 0 || !fragSpirv || fragWordCount == 0)
		{
			LOG_WARN(Render, "[UnderwaterPass] Init skipped: shaders not provided");
			return false;
		}

		m_maxFrames = (maxFrames > 0) ? maxFrames : 1;

		// ── 1. Depth sampler (NEAREST, clamp-to-edge) ────────────────────────
		{
			VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			si.magFilter    = VK_FILTER_NEAREST;
			si.minFilter    = VK_FILTER_NEAREST;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.maxLod       = 0.0f;
			if (vkCreateSampler(device, &si, nullptr, &m_sampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: sampler creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 2. Render pass — composite overlay onto SceneColorHDR ────────────
		// LOAD_OP_LOAD preserves the existing scene; alpha blending composites the overlay.
		{
			VkAttachmentDescription colorAtt{};
			colorAtt.format         = sceneColorHDRFormat;
			colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
			colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAtt.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorRef{};
			colorRef.attachment = 0;
			colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments    = &colorRef;

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
			rci.attachmentCount = 1;
			rci.pAttachments    = &colorAtt;
			rci.subpassCount    = 1;
			rci.pSubpasses      = &subpass;
			rci.dependencyCount = 1;
			rci.pDependencies   = &dep;

			if (vkCreateRenderPass(device, &rci, nullptr, &m_renderPass) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: render pass creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 3. Descriptor set layout — binding 0: sceneDepth ─────────────────
		{
			VkDescriptorSetLayoutBinding binding{};
			binding.binding         = 0;
			binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			binding.descriptorCount = 1;
			binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			dlci.bindingCount = 1;
			dlci.pBindings    = &binding;
			if (vkCreateDescriptorSetLayout(device, &dlci, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: descriptor set layout creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 4. Descriptor pool + sets ─────────────────────────────────────────
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = m_maxFrames; ///< 1 depth sampler per frame.

			VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			dpci.maxSets       = m_maxFrames;
			dpci.poolSizeCount = 1;
			dpci.pPoolSizes    = &poolSize;
			if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: descriptor pool creation failed");
				Destroy(device);
				return false;
			}

			m_descriptorSets.resize(m_maxFrames, VK_NULL_HANDLE);
			for (uint32_t i = 0; i < m_maxFrames; ++i)
			{
				VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
				dsai.descriptorPool     = m_descriptorPool;
				dsai.descriptorSetCount = 1;
				dsai.pSetLayouts        = &m_descriptorSetLayout;
				if (vkAllocateDescriptorSets(device, &dsai, &m_descriptorSets[i]) != VK_SUCCESS)
				{
					LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: descriptor set allocation failed");
					Destroy(device);
					return false;
				}
			}
			// Depth views are written per-frame in Record() since the view is FG-managed.
		}

		// ── 5. Pipeline layout (push constants: 32 bytes, fragment stage) ────
		{
			VkPushConstantRange pcr{};
			pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pcr.offset     = 0;
			pcr.size       = static_cast<uint32_t>(sizeof(UnderwaterParams));

			VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			plci.setLayoutCount         = 1;
			plci.pSetLayouts            = &m_descriptorSetLayout;
			plci.pushConstantRangeCount = 1;
			plci.pPushConstantRanges    = &pcr;

			if (vkCreatePipelineLayout(device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: pipeline layout creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 6. Graphics pipeline ─────────────────────────────────────────────
		{
			VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			VkShaderModule vertMod = VK_NULL_HANDLE;
			VkShaderModule fragMod = VK_NULL_HANDLE;

			smci.codeSize = vertWordCount * sizeof(uint32_t);
			smci.pCode    = vertSpirv;
			if (vkCreateShaderModule(device, &smci, nullptr, &vertMod) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: vertex shader module creation failed");
				Destroy(device);
				return false;
			}
			smci.codeSize = fragWordCount * sizeof(uint32_t);
			smci.pCode    = fragSpirv;
			if (vkCreateShaderModule(device, &smci, nullptr, &fragMod) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: fragment shader module creation failed");
				vkDestroyShaderModule(device, vertMod, nullptr);
				Destroy(device);
				return false;
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertMod;
			stages[0].pName  = "main";
			stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragMod;
			stages[1].pName  = "main";

			// No vertex buffers — fullscreen triangle generated from gl_VertexIndex.
			VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

			VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
			vps.viewportCount = 1;
			vps.scissorCount  = 1;

			VkPipelineRasterizationStateCreateInfo ras{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
			ras.polygonMode = VK_POLYGON_MODE_FILL;
			ras.cullMode    = VK_CULL_MODE_NONE;
			ras.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			ras.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Alpha blending: composite underwater overlay onto existing scene color.
			VkPipelineColorBlendAttachmentState blendAtt{};
			blendAtt.blendEnable         = VK_TRUE;
			blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
			blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
			blendAtt.colorWriteMask      =
				VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
			cbs.attachmentCount = 1;
			cbs.pAttachments    = &blendAtt;

			VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
			ds.depthTestEnable  = VK_FALSE;
			ds.depthWriteEnable = VK_FALSE;

			constexpr VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynStates;

			VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
			gpci.stageCount          = 2;
			gpci.pStages             = stages;
			gpci.pVertexInputState   = &vi;
			gpci.pInputAssemblyState = &ia;
			gpci.pViewportState      = &vps;
			gpci.pRasterizationState = &ras;
			gpci.pMultisampleState   = &ms;
			gpci.pDepthStencilState  = &ds;
			gpci.pColorBlendState    = &cbs;
			gpci.pDynamicState       = &dyn;
			gpci.layout              = m_pipelineLayout;
			gpci.renderPass          = m_renderPass;
			gpci.subpass             = 0;

			const VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpci, nullptr, &m_pipeline);

			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);

			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: graphics pipeline creation failed");
				Destroy(device);
				return false;
			}
		}

		LOG_INFO(Render, "[UnderwaterPass] Init OK (maxFrames={})", m_maxFrames);
		return true;
	}

	void UnderwaterPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent,
		ResourceId idSceneColorHDR,
		ResourceId idDepth,
		const UnderwaterParams& params,
		uint32_t frameIndex)
	{
		if (!IsValid())
			return;

		const VkImageView hdrView   = registry.getImageView(idSceneColorHDR);
		const VkImageView depthView = registry.getImageView(idDepth);
		if (hdrView == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[UnderwaterPass] Record skipped: SceneColorHDR view not bound");
			return;
		}

		// Update the depth descriptor before rendering (FG view changes per frame).
		const uint32_t fi = frameIndex % m_maxFrames;
		if (depthView != VK_NULL_HANDLE)
		{
			VkDescriptorImageInfo depthInfo{};
			depthInfo.sampler     = m_sampler;
			depthInfo.imageView   = depthView;
			depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
			write.dstSet          = m_descriptorSets[fi];
			write.dstBinding      = 0;
			write.descriptorCount = 1;
			write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo      = &depthInfo;
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}

		// Create a temporary framebuffer for this extent (destroyed after the render pass).
		VkFramebuffer fb = VK_NULL_HANDLE;
		{
			VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
			fci.renderPass      = m_renderPass;
			fci.attachmentCount = 1;
			fci.pAttachments    = &hdrView;
			fci.width           = extent.width;
			fci.height          = extent.height;
			fci.layers          = 1;
			if (vkCreateFramebuffer(device, &fci, nullptr, &fb) != VK_SUCCESS)
			{
				LOG_WARN(Render, "[UnderwaterPass] Record: framebuffer creation failed — skipping");
				return;
			}
		}

		VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
		rpbi.renderPass        = m_renderPass;
		rpbi.framebuffer       = fb;
		rpbi.renderArea.extent = extent;

		vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

		const VkViewport vp{0.0f, 0.0f,
			static_cast<float>(extent.width), static_cast<float>(extent.height),
			0.0f, 1.0f};
		const VkRect2D sc{{0, 0}, extent};
		vkCmdSetViewport(cmd, 0, 1, &vp);
		vkCmdSetScissor(cmd, 0, 1, &sc);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_pipelineLayout, 0, 1, &m_descriptorSets[fi], 0, nullptr);

		vkCmdPushConstants(cmd, m_pipelineLayout,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(UnderwaterParams), &params);

		// Draw the fullscreen triangle (3 vertices, no VBO).
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);

		vkDestroyFramebuffer(device, fb, nullptr);

		LOG_TRACE(Render, "[UnderwaterPass] Record (frame={})", frameIndex);
	}

	void UnderwaterPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (m_pipeline            != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
		if (m_pipelineLayout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
		if (m_descriptorPool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
		if (m_descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
		if (m_renderPass          != VK_NULL_HANDLE) vkDestroyRenderPass(device, m_renderPass, nullptr);
		if (m_sampler             != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);

		m_pipeline            = VK_NULL_HANDLE;
		m_pipelineLayout      = VK_NULL_HANDLE;
		m_descriptorPool      = VK_NULL_HANDLE;
		m_descriptorSetLayout = VK_NULL_HANDLE;
		m_renderPass          = VK_NULL_HANDLE;
		m_sampler             = VK_NULL_HANDLE;
		m_descriptorSets.clear();

		LOG_INFO(Render, "[UnderwaterPass] Destroyed");
	}

} // namespace engine::render
