#include "engine/render/UnderwaterPass.h"
#include "engine/render/PipelineCache.h"
#include "engine/render/PsoKey.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>
#include <array>
#include <cstring>
#include <vector>

namespace engine::render
{
	// -------------------------------------------------------------------------
	// Internal helpers
	// -------------------------------------------------------------------------

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
	} // namespace

	// -------------------------------------------------------------------------
	// UnderwaterPass::Init
	// -------------------------------------------------------------------------

	bool UnderwaterPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
	                          VkFormat sceneColorHDRFormat,
	                          const uint32_t* vertSpirv, size_t vertWordCount,
	                          const uint32_t* fragSpirv, size_t fragWordCount,
	                          uint32_t maxFrames,
	                          VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
		    || vertWordCount == 0 || fragWordCount == 0)
		{
			LOG_ERROR(Render, "[UnderwaterPass] Init FAILED: invalid parameters");
			return false;
		}

		m_maxFrames = (maxFrames > 0) ? maxFrames : 1u;

		// -----------------------------------------------------------------
		// 1. Render pass: 1 colour attachment (UnderwaterHDR, same format as scene)
		//    LOAD_OP_DONT_CARE because we overwrite every pixel.
		// -----------------------------------------------------------------
		{
			VkAttachmentDescription colorAtt{};
			colorAtt.format         = sceneColorHDRFormat;
			colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
			colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fullscreen overwrite
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

			// Ensure that any previous colour-attachment writes to SceneColorHDR have
			// completed before we sample it as a texture in the fragment shader.
			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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

			VkResult res = vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkCreateRenderPass failed: {}", static_cast<int>(res));
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 2. Descriptor set layout:
		//    binding 0 = SceneColor_HDR  (combined image sampler)
		//    binding 1 = depth texture   (combined image sampler)
		// -----------------------------------------------------------------
		{
			VkDescriptorSetLayoutBinding bindings[2]{};
			bindings[0].binding         = 0;
			bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[0].descriptorCount = 1;
			bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[1].binding         = 1;
			bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[1].descriptorCount = 1;
			bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 2;
			layoutInfo.pBindings    = bindings;

			VkResult res = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkCreateDescriptorSetLayout failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 3. Descriptor pool: maxFrames sets, 2 combined image samplers each
		// -----------------------------------------------------------------
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = m_maxFrames * 2;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = m_maxFrames;

			VkResult res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkCreateDescriptorPool failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 4. Allocate one descriptor set per frame
		// -----------------------------------------------------------------
		{
			std::vector<VkDescriptorSetLayout> layouts(m_maxFrames, m_descriptorSetLayout);

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool     = m_descriptorPool;
			allocInfo.descriptorSetCount = m_maxFrames;
			allocInfo.pSetLayouts        = layouts.data();

			m_descriptorSets.resize(m_maxFrames, VK_NULL_HANDLE);
			VkResult res = vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data());
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkAllocateDescriptorSets failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 5a. Linear clamp sampler for SceneColor_HDR
		// -----------------------------------------------------------------
		{
			VkSamplerCreateInfo si{};
			si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter    = VK_FILTER_LINEAR;
			si.minFilter    = VK_FILTER_LINEAR;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.compareEnable = VK_FALSE;
			si.maxLod        = 0.0f;

			VkResult res = vkCreateSampler(device, &si, nullptr, &m_colorSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkCreateSampler (color) failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 5b. Nearest clamp sampler for depth texture
		// -----------------------------------------------------------------
		{
			VkSamplerCreateInfo si{};
			si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter    = VK_FILTER_NEAREST;
			si.minFilter    = VK_FILTER_NEAREST;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.compareEnable = VK_FALSE;
			si.maxLod        = 0.0f;

			VkResult res = vkCreateSampler(device, &si, nullptr, &m_depthSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkCreateSampler (depth) failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 6. Pipeline layout: descriptor set 0 + push constants (UnderwaterParams, 16 bytes)
		// -----------------------------------------------------------------
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pushRange.offset     = 0;
			pushRange.size       = static_cast<uint32_t>(sizeof(UnderwaterParams)); // 16 bytes

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount         = 1;
			layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushRange;

			VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkCreatePipelineLayout failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 7. Graphics pipeline: fullscreen triangle, no vertex input, no depth test
		// -----------------------------------------------------------------
		{
			VkShaderModule vertMod = CreateShaderModule(device, vertSpirv, vertWordCount);
			VkShaderModule fragMod = CreateShaderModule(device, fragSpirv, fragWordCount);
			if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "[UnderwaterPass] shader module creation failed");
				if (vertMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertMod, nullptr);
				if (fragMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragMod, nullptr);
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

			// No vertex input: fullscreen triangle generated from gl_VertexIndex.
			VkPipelineVertexInputStateCreateInfo vi{};
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vp{};
			vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount  = 1;

			VkPipelineRasterizationStateCreateInfo rs{};
			rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode    = VK_CULL_MODE_NONE;
			rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo dss{};
			dss.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			dss.depthTestEnable  = VK_FALSE;
			dss.depthWriteEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState blendAtt{};
			blendAtt.blendEnable    = VK_FALSE;
			blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo cb{};
			cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1;
			cb.pAttachments    = &blendAtt;

			VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynStates;

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
			PipelineCache::RegisterWarmupKey(HashGraphicsPsoKey(
			    m_renderPass, 0, m_pipelineLayout, sceneColorHDRFormat, VK_FORMAT_UNDEFINED));

			VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpInfo, nullptr, &m_pipeline);
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[UnderwaterPass] vkCreateGraphicsPipelines failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		LOG_INFO(Render, "[UnderwaterPass] Init OK (maxFrames={})", m_maxFrames);
		return true;
	}

	// -------------------------------------------------------------------------
	// UnderwaterPass::Record
	// -------------------------------------------------------------------------

	void UnderwaterPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
	                            VkExtent2D extent,
	                            ResourceId idSceneColorHDR,
	                            ResourceId idDepth,
	                            ResourceId idUnderwaterHDR,
	                            const UnderwaterParams& params,
	                            uint32_t frameIndex)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0)
			return;

		VkImageView viewHDR         = registry.getImageView(idSceneColorHDR);
		VkImageView viewDepth       = registry.getImageView(idDepth);
		VkImageView viewUnderwaterOut = registry.getImageView(idUnderwaterHDR);

		if (viewHDR == VK_NULL_HANDLE || viewUnderwaterOut == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[UnderwaterPass] Record: missing image views, skipping");
			return;
		}

		// Use the HDR view as fallback when depth is not bound.
		VkImageView safeDepthView = (viewDepth != VK_NULL_HANDLE) ? viewDepth : viewHDR;

		const uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		// Update descriptor set for this frame: binding 0 = color HDR, binding 1 = depth.
		VkDescriptorImageInfo imageInfos[2]{};
		imageInfos[0].sampler     = m_colorSampler;
		imageInfos[0].imageView   = viewHDR;
		imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos[1].sampler     = m_depthSampler;
		imageInfos[1].imageView   = safeDepthView;
		imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet writes[2]{};
		writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet          = ds;
		writes[0].dstBinding      = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo      = &imageInfos[0];
		writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet          = ds;
		writes[1].dstBinding      = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo      = &imageInfos[1];
		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

		// Create a temporary framebuffer for the underwater output RT.
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments    = &viewUnderwaterOut;
		fbInfo.width           = extent.width;
		fbInfo.height          = extent.height;
		fbInfo.layers          = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;
		VkResult res = vkCreateFramebuffer(device, &fbInfo, nullptr, &fb);
		if (res != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[UnderwaterPass] vkCreateFramebuffer failed: {}", static_cast<int>(res));
			return;
		}

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass  = m_renderPass;
		rpBegin.framebuffer = fb;
		rpBegin.renderArea  = { { 0, 0 }, extent };
		// No clear values (DONT_CARE).

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        m_pipelineLayout, 0, 1, &ds, 0, nullptr);

		VkViewport viewport{};
		viewport.width    = static_cast<float>(extent.width);
		viewport.height   = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0, static_cast<uint32_t>(sizeof(UnderwaterParams)), &params);

		// Draw fullscreen triangle (3 vertices, no vertex buffer).
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);

		vkDestroyFramebuffer(device, fb, nullptr);

		LOG_TRACE(Render, "[UnderwaterPass] Record OK (underwaterFactor={:.2f})", params.underwaterFactor);
	}

	// -------------------------------------------------------------------------
	// UnderwaterPass::Destroy
	// -------------------------------------------------------------------------

	void UnderwaterPass::Destroy(VkDevice device)
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
		if (m_colorSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_colorSampler, nullptr);
			m_colorSampler = VK_NULL_HANDLE;
		}
		if (m_depthSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_depthSampler, nullptr);
			m_depthSampler = VK_NULL_HANDLE;
		}
		m_descriptorSets.clear();
		if (m_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
		}
		if (m_descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
			m_descriptorSetLayout = VK_NULL_HANDLE;
		}
		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}

		LOG_INFO(Render, "[UnderwaterPass] Destroyed");
	}

} // namespace engine::render
