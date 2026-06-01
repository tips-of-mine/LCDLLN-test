#include "src/client/render/VolumetricFogPass.h"
#include "src/client/render/PipelineCache.h"
#include "src/shared/core/Log.h"

#include <array>
#include <cstring>

namespace engine::render
{
	// -------------------------------------------------------------------------
	// Helpers internes
	// -------------------------------------------------------------------------

	namespace
	{
		/// Crée un VkShaderModule à partir de mots SPIR-V.
		/// \param code      Pointeur sur les mots SPIR-V.
		/// \param wordCount Nombre de mots (32 bits) du module.
		/// \return Le module créé, ou VK_NULL_HANDLE en cas d'échec.
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

	} // namespace anonyme

	// -------------------------------------------------------------------------
	// VolumetricFogPass::Init
	// -------------------------------------------------------------------------

	bool VolumetricFogPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxFrames,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
			|| vertWordCount == 0 || fragWordCount == 0)
		{
			LOG_ERROR(Render, "VolumetricFogPass::Init: invalid arguments");
			return false;
		}

		m_maxFrames = maxFrames > 0 ? maxFrames : 1;

		// -----------------------------------------------------------------
		// 1. Render pass : 1 color attachment (SceneColor_HDR brouillardée)
		// -----------------------------------------------------------------
		{
			VkAttachmentDescription colorAtt{};
			colorAtt.format         = sceneColorHDRFormat;
			colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
			colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // on réécrit tout le plein écran
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

			// Dépendance : on lit scene color / depth / shadow map (écrits par les passes
			// amont en color/depth) en fragment shader avant de réécrire le plein écran.
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

			VkResult res = vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "VolumetricFogPass: vkCreateRenderPass failed: {}", static_cast<int>(res));
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 2. Descriptor set layout : 3 combined image samplers
		//    (scene color HDR, depth, shadow map cascade 0)
		// -----------------------------------------------------------------
		{
			std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
			for (size_t i = 0; i < bindings.size(); ++i)
			{
				bindings[i].binding            = static_cast<uint32_t>(i);
				bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount    = 1;
				bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings[i].pImmutableSamplers = nullptr;
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			layoutInfo.pBindings    = bindings.data();

			VkResult res = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "VolumetricFogPass: vkCreateDescriptorSetLayout failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 3. Descriptor pool : maxFrames sets, 3 combined image samplers chacun
		// -----------------------------------------------------------------
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 3 * m_maxFrames;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = m_maxFrames;

			VkResult res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "VolumetricFogPass: vkCreateDescriptorPool failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 4. Alloue un descriptor set par frame
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
				LOG_ERROR(Render, "VolumetricFogPass: vkAllocateDescriptorSets failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 5. Samplers : linéaire clamp (scene color), nearest clamp (depth + shadow)
		// -----------------------------------------------------------------
		{
			VkSamplerCreateInfo si{};
			si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter     = VK_FILTER_LINEAR;
			si.minFilter     = VK_FILTER_LINEAR;
			si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.compareEnable = VK_FALSE;
			si.compareOp     = VK_COMPARE_OP_ALWAYS;
			si.maxLod        = 0.0f;

			VkResult res = vkCreateSampler(device, &si, nullptr, &m_linearSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "VolumetricFogPass: vkCreateSampler (linear) failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}

			// Nearest clamp : depth + shadow map (lecture plain, pas de compare).
			si.magFilter = VK_FILTER_NEAREST;
			si.minFilter = VK_FILTER_NEAREST;
			res = vkCreateSampler(device, &si, nullptr, &m_nearestSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "VolumetricFogPass: vkCreateSampler (nearest) failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 6. Pipeline layout : descriptor set 0 + push constants (192 o, fragment)
		// -----------------------------------------------------------------
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pushRange.offset     = 0;
			pushRange.size       = static_cast<uint32_t>(sizeof(FogParams)); // 192 octets

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount         = 1;
			layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushRange;

			VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "VolumetricFogPass: vkCreatePipelineLayout failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 7. Pipeline graphique : fullscreen triangle, pas de vertex input, pas de depth test
		// -----------------------------------------------------------------
		{
			VkShaderModule vertMod = CreateShaderModule(device, vertSpirv, vertWordCount);
			VkShaderModule fragMod = CreateShaderModule(device, fragSpirv, fragWordCount);
			if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "VolumetricFogPass: shader module creation failed");
				if (vertMod) vkDestroyShaderModule(device, vertMod, nullptr);
				if (fragMod) vkDestroyShaderModule(device, fragMod, nullptr);
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

			// Pas de vertex input : le triangle plein écran est généré depuis gl_VertexIndex.
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
			rs.cullMode    = VK_CULL_MODE_NONE; // pas de culling pour le triangle plein écran
			rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Pas de depth test/write pour la passe plein écran.
			VkPipelineDepthStencilStateCreateInfo ds{};
			ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable  = VK_FALSE;
			ds.depthWriteEnable = VK_FALSE;

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
			gpInfo.pDepthStencilState  = &ds;
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
				LOG_ERROR(Render, "VolumetricFogPass: vkCreateGraphicsPipelines failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		LOG_INFO(Render, "VolumetricFogPass: initialized (maxFrames={})", m_maxFrames);
		return true;
	}

	// -------------------------------------------------------------------------
	// VolumetricFogPass::Record
	// -------------------------------------------------------------------------

	void VolumetricFogPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent,
		ResourceId idSceneColorIn, ResourceId idDepth, ResourceId idShadowCascade0,
		ResourceId idSceneColorOut, const FogParams& params, uint32_t frameIndex)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0)
			return;

		// Récupère les vues depuis le registry du frame graph.
		VkImageView viewSceneIn = registry.getImageView(idSceneColorIn);
		VkImageView viewDepth   = registry.getImageView(idDepth);
		VkImageView viewShadow  = registry.getImageView(idShadowCascade0);
		VkImageView viewOut     = registry.getImageView(idSceneColorOut);

		if (viewSceneIn == VK_NULL_HANDLE || viewDepth == VK_NULL_HANDLE
			|| viewShadow == VK_NULL_HANDLE || viewOut == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "VolumetricFogPass::Record: missing image views, skipping");
			return;
		}

		// ------------------------------------------------------------------
		// Met à jour le descriptor set de cette frame avec les 3 vues.
		// ------------------------------------------------------------------
		const uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		std::array<VkDescriptorImageInfo, 3> imageInfos{};
		imageInfos[0] = { m_linearSampler,  viewSceneIn, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_nearestSampler, viewDepth,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[2] = { m_nearestSampler, viewShadow,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		std::array<VkWriteDescriptorSet, 3> writes{};
		for (size_t i = 0; i < writes.size(); ++i)
		{
			writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet          = ds;
			writes[i].dstBinding      = static_cast<uint32_t>(i);
			writes[i].dstArrayElement = 0;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo      = &imageInfos[i];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		// ------------------------------------------------------------------
		// Crée un framebuffer temporaire sur l'image de sortie.
		// ------------------------------------------------------------------
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments    = &viewOut;
		fbInfo.width           = extent.width;
		fbInfo.height          = extent.height;
		fbInfo.layers          = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;
		VkResult res = vkCreateFramebuffer(device, &fbInfo, nullptr, &fb);
		if (res != VK_SUCCESS)
		{
			LOG_ERROR(Render, "VolumetricFogPass::Record: vkCreateFramebuffer failed: {}", static_cast<int>(res));
			return;
		}

		// ------------------------------------------------------------------
		// Ouvre la render pass.
		// ------------------------------------------------------------------
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

		// Push constants : FogParams (192 octets, stage fragment).
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			0, static_cast<uint32_t>(sizeof(FogParams)), &params);

		// Triangle plein écran (3 sommets, pas de vertex buffer).
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);

		// Détruit le framebuffer temporaire immédiatement.
		vkDestroyFramebuffer(device, fb, nullptr);
	}

	// -------------------------------------------------------------------------
	// VolumetricFogPass::Destroy
	// -------------------------------------------------------------------------

	void VolumetricFogPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
		{
			LOG_INFO(Render, "[VolumetricFogPass] Destroyed");
			return;
		}

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
		if (m_nearestSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_nearestSampler, nullptr);
			m_nearestSampler = VK_NULL_HANDLE;
		}
		if (m_linearSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_linearSampler, nullptr);
			m_linearSampler = VK_NULL_HANDLE;
		}
		// Les descriptor sets sont libérés implicitement à la destruction du pool.
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
		LOG_INFO(Render, "[VolumetricFogPass] Destroyed");
	}

} // namespace engine::render
