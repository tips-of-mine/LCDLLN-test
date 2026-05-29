// src/client/render/WaterPass.cpp
#include "src/client/render/WaterPass.h"
#include "src/client/render/PipelineCache.h"
#include "src/client/render/PsoKey.h"
#include "src/shared/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <vector>

namespace engine::render
{
	static_assert(sizeof(engine::render::WaterVertex) == 28,
		"WaterPass pipeline assumes WaterVertex is 28 bytes (pos3 + uv2 + flowDir2)");

	// -------------------------------------------------------------------------
	// Internal helpers
	// -------------------------------------------------------------------------

	namespace
	{
		static constexpr uint32_t kWaterBindingCount = 4;  // sceneColor + sceneDepth + normalMap + skybox

		/// Crée un VkShaderModule depuis un buffer SPIR-V.
		/// \param wordCount  Nombre de uint32_t (pas d'octets) dans \p code.
		/// \return VK_NULL_HANDLE en cas d'erreur (vkCreateShaderModule loggué par l'appelant).
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
	// WaterPass::Init
	// -------------------------------------------------------------------------

	bool WaterPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		VkImageView normalMapView, VkSampler normalMapSampler,
		VkImageView skyboxCubeView, VkSampler skyboxSampler,
		uint32_t maxFrames,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
		    || vertWordCount == 0 || fragWordCount == 0
		    || normalMapView == VK_NULL_HANDLE || normalMapSampler == VK_NULL_HANDLE
		    || skyboxCubeView == VK_NULL_HANDLE || skyboxSampler == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: invalid arguments");
			return false;
		}

		m_maxFrames        = (maxFrames > 0) ? maxFrames : 1u;
		m_normalMapView    = normalMapView;
		m_normalMapSampler = normalMapSampler;
		m_skyboxCubeView   = skyboxCubeView;
		m_skyboxSampler    = skyboxSampler;

		// -----------------------------------------------------------------
		// 1. Render pass : 1 color attachment (SceneColor_HDR_PostWater).
		//    LOAD_OP_DONT_CARE — la passe écrit par-dessus via blend.
		// -----------------------------------------------------------------
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

			// La dépendance garantit que les writes color + lectures shader de la passe
			// précédente (sceneColor en tant que texture) sont terminés avant qu'on écrive.
			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			                  | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
			                  | VK_ACCESS_SHADER_READ_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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
				LOG_ERROR(Render, "[WaterPass] vkCreateRenderPass failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		// -----------------------------------------------------------------
		// 2. Descriptor set layout : 4 bindings combined image sampler
		//    binding 0 = sceneColor, 1 = sceneDepth, 2 = normalMap, 3 = skyboxCube
		// -----------------------------------------------------------------
		{
			VkDescriptorSetLayoutBinding bindings[kWaterBindingCount]{};
			for (int i = 0; i < kWaterBindingCount; ++i)
			{
				bindings[i].binding         = static_cast<uint32_t>(i);
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = kWaterBindingCount;
			layoutInfo.pBindings    = bindings;

			VkResult res = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateDescriptorSetLayout failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		// -----------------------------------------------------------------
		// 3. Descriptor pool : 4 samplers × maxFrames sets
		// -----------------------------------------------------------------
		{
			VkDescriptorPoolSize sizes[1]{};
			sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			sizes[0].descriptorCount = kWaterBindingCount * m_maxFrames;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = sizes;
			poolInfo.maxSets       = m_maxFrames;

			VkResult res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateDescriptorPool failed: {}", static_cast<int>(res));
				Destroy(device); return false;
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
				LOG_ERROR(Render, "[WaterPass] vkAllocateDescriptorSets failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		// -----------------------------------------------------------------
		// 5a. Sampler linear clamp pour sceneColor
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

			VkResult res = vkCreateSampler(device, &si, nullptr, &m_sceneColorSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateSampler (sceneColor) failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		// -----------------------------------------------------------------
		// 5b. Sampler nearest clamp pour sceneDepth
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

			VkResult res = vkCreateSampler(device, &si, nullptr, &m_sceneDepthSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateSampler (sceneDepth) failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		// -----------------------------------------------------------------
		// 6. Pipeline layout : 1 descriptor set + push constants 128 B vertex+fragment
		// -----------------------------------------------------------------
		{
			VkPushConstantRange pcRange{};
			pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pcRange.offset     = 0;
			pcRange.size       = static_cast<uint32_t>(sizeof(WaterPassPushConstants));  // 128 B

			VkPipelineLayoutCreateInfo li{};
			li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount         = 1;
			li.pSetLayouts            = &m_descriptorSetLayout;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges    = &pcRange;

			VkResult res = vkCreatePipelineLayout(device, &li, nullptr, &m_pipelineLayout);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreatePipelineLayout failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		// -----------------------------------------------------------------
		// 7. Graphics pipeline : vertex format WaterVertex 28 B,
		//    alpha blend, depth test/write OFF, cull BACK, dynamic viewport/scissor
		// -----------------------------------------------------------------
		{
			VkShaderModule vertMod = CreateShaderModule(device, vertSpirv, vertWordCount);
			VkShaderModule fragMod = CreateShaderModule(device, fragSpirv, fragWordCount);
			if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "[WaterPass] shader module creation failed");
				if (vertMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertMod, nullptr);
				if (fragMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragMod, nullptr);
				Destroy(device); return false;
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

			// Vertex binding : WaterVertex = pos3 (offset 0) + uv2 (offset 12) + flowDir2 (offset 20) = 28 B
			VkVertexInputBindingDescription vboBinding{};
			vboBinding.binding   = 0;
			vboBinding.stride    = 28;  // sizeof(WaterVertex)
			vboBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attribs[3]{};
			attribs[0].location = 0; attribs[0].binding = 0;
			attribs[0].format   = VK_FORMAT_R32G32B32_SFLOAT; attribs[0].offset = 0;   // position
			attribs[1].location = 1; attribs[1].binding = 0;
			attribs[1].format   = VK_FORMAT_R32G32_SFLOAT;    attribs[1].offset = 12;  // uv
			attribs[2].location = 2; attribs[2].binding = 0;
			attribs[2].format   = VK_FORMAT_R32G32_SFLOAT;    attribs[2].offset = 20;  // flowDir

			VkPipelineVertexInputStateCreateInfo vi{};
			vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vi.vertexBindingDescriptionCount   = 1;
			vi.pVertexBindingDescriptions      = &vboBinding;
			vi.vertexAttributeDescriptionCount = 3;
			vi.pVertexAttributeDescriptions    = attribs;

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
			// Surface visible de dessus (joueur qui regarde depuis la rive)
			// ET de dessous (joueur qui nage, regarde la surface depuis l'eau).
			rs.cullMode    = VK_CULL_MODE_NONE;
			rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Pas de depth attachment dans cette passe (output color seul).
			VkPipelineDepthStencilStateCreateInfo ds{};
			ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable  = VK_FALSE;
			ds.depthWriteEnable = VK_FALSE;
			ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

			// Alpha blend standard : src_alpha / one_minus_src_alpha
			VkPipelineColorBlendAttachmentState blend{};
			blend.blendEnable         = VK_TRUE;
			blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend.colorBlendOp        = VK_BLEND_OP_ADD;
			blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blend.alphaBlendOp        = VK_BLEND_OP_ADD;
			blend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo cb{};
			cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1;
			cb.pAttachments    = &blend;

			VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynStates;

			VkGraphicsPipelineCreateInfo pi{};
			pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pi.stageCount          = 2;
			pi.pStages             = stages;
			pi.pVertexInputState   = &vi;
			pi.pInputAssemblyState = &ia;
			pi.pViewportState      = &vp;
			pi.pRasterizationState = &rs;
			pi.pMultisampleState   = &ms;
			pi.pDepthStencilState  = &ds;
			pi.pColorBlendState    = &cb;
			pi.pDynamicState       = &dyn;
			pi.layout              = m_pipelineLayout;
			pi.renderPass          = m_renderPass;
			pi.subpass             = 0;

			AssertPipelineCreationAllowed();
			PipelineCache::RegisterWarmupKey(HashGraphicsPsoKey(
			    m_renderPass, 0, m_pipelineLayout, sceneColorHDRFormat, VK_FORMAT_UNDEFINED));

			VkResult r = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pi, nullptr, &m_pipeline);
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			if (r != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateGraphicsPipelines failed: {}", static_cast<int>(r));
				Destroy(device); return false;
			}
		}

		LOG_INFO(Render, "[WaterPass] Init OK (maxFrames={})", m_maxFrames);
		return true;
	}

	// -------------------------------------------------------------------------
	// WaterPass::Destroy
	// -------------------------------------------------------------------------

	void WaterPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;

		InvalidateFramebufferCache(device);

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
		if (m_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
		}
		m_descriptorSets.clear();
		if (m_descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
			m_descriptorSetLayout = VK_NULL_HANDLE;
		}
		if (m_sceneColorSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_sceneColorSampler, nullptr);
			m_sceneColorSampler = VK_NULL_HANDLE;
		}
		if (m_sceneDepthSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_sceneDepthSampler, nullptr);
			m_sceneDepthSampler = VK_NULL_HANDLE;
		}
		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}

		// Réinitialise les vues externes (non possédées, pas à détruire).
		m_normalMapView    = VK_NULL_HANDLE;
		m_normalMapSampler = VK_NULL_HANDLE;
		m_skyboxCubeView   = VK_NULL_HANDLE;
		m_skyboxSampler    = VK_NULL_HANDLE;

		LOG_INFO(Render, "[WaterPass] Destroyed");
	}

	// -------------------------------------------------------------------------
	// WaterPass::InvalidateFramebufferCache
	// -------------------------------------------------------------------------

	void WaterPass::InvalidateFramebufferCache(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;

		for (auto& kv : m_framebufferCache)
		{
			if (kv.second != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device, kv.second, nullptr);
		}
		m_framebufferCache.clear();
	}

	// -------------------------------------------------------------------------
	// WaterPass::Record
	// -------------------------------------------------------------------------

	void WaterPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent,
		ResourceId idSceneColorIn,
		ResourceId idSceneDepth,
		ResourceId idSceneColorOut,
		const WaterMeshGpu& mesh,
		const WaterPassPushConstants& paramsBase,
		const engine::world::water::WaterScene& scene,
		uint32_t frameIndex)
	{
		if (!IsValid()) return;
		if (mesh.GetInstanceCount() == 0) return;
		if (frameIndex >= m_maxFrames) return;

		VkImage     colorOut     = registry.getImage(idSceneColorOut);
		VkImageView colorOutView = registry.getImageView(idSceneColorOut);
		VkImageView sceneInView  = registry.getImageView(idSceneColorIn);
		VkImageView depthView    = registry.getImageView(idSceneDepth);
		if (colorOut == VK_NULL_HANDLE || colorOutView == VK_NULL_HANDLE
		    || sceneInView == VK_NULL_HANDLE || depthView == VK_NULL_HANDLE)
			return;

		// 1. Update descriptor set for this frame.
		{
			VkDescriptorImageInfo info[kWaterBindingCount]{};
			info[0].sampler     = m_sceneColorSampler;
			info[0].imageView   = sceneInView;
			info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info[1].sampler     = m_sceneDepthSampler;
			info[1].imageView   = depthView;
			info[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info[2].sampler     = m_normalMapSampler;
			info[2].imageView   = m_normalMapView;
			info[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info[3].sampler     = m_skyboxSampler;
			info[3].imageView   = m_skyboxCubeView;
			info[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet writes[kWaterBindingCount]{};
			for (uint32_t i = 0; i < kWaterBindingCount; ++i)
			{
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = m_descriptorSets[frameIndex];
				writes[i].dstBinding      = i;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &info[i];
			}
			vkUpdateDescriptorSets(device, kWaterBindingCount, writes, 0, nullptr);
		}

		// 2. Framebuffer cache (keyed par output view + extent).
		FramebufferKey key{ colorOutView, extent.width, extent.height };
		VkFramebuffer fb = VK_NULL_HANDLE;
		auto it = m_framebufferCache.find(key);
		if (it != m_framebufferCache.end())
		{
			fb = it->second;
		}
		else
		{
			VkFramebufferCreateInfo fbi{};
			fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbi.renderPass      = m_renderPass;
			fbi.attachmentCount = 1;
			fbi.pAttachments    = &colorOutView;
			fbi.width           = extent.width;
			fbi.height          = extent.height;
			fbi.layers          = 1;
			if (vkCreateFramebuffer(device, &fbi, nullptr, &fb) != VK_SUCCESS)
			{
				LOG_WARN(Render, "[WaterPass] vkCreateFramebuffer failed");
				return;
			}
			m_framebufferCache[key] = fb;
		}

		// 3. Begin render pass.
		VkRenderPassBeginInfo rpb{};
		rpb.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpb.renderPass        = m_renderPass;
		rpb.framebuffer       = fb;
		rpb.renderArea.extent = extent;
		rpb.clearValueCount   = 0;  // LOAD_OP_DONT_CARE
		vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport vp{};
		vp.x        = 0;
		vp.y        = 0;
		vp.width    = static_cast<float>(extent.width);
		vp.height   = static_cast<float>(extent.height);
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &vp);
		VkRect2D sc{ { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &sc);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
		                        0, 1, &m_descriptorSets[frameIndex], 0, nullptr);

		VkBuffer     vbo  = mesh.GetVertexBuffer();
		VkBuffer     ibo  = mesh.GetIndexBuffer();
		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &zero);
		vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);

		// 4. Loop drawInfos avec push constants par-instance.
		const auto&    drawInfos = mesh.GetDrawInfos();
		const uint32_t nLakes    = static_cast<uint32_t>(scene.lakes.size());

		// Défauts hardcodés pour les rivières (RiverInstance n'a pas
		// bottomColor/turbidity/flowSpeed dans M100.13).
		constexpr float kRiverDefaultBottomColor[3] = { 0.05f, 0.15f, 0.20f };
		constexpr float kRiverDefaultTurbidity      = 0.4f;
		constexpr float kRiverDefaultFlowSpeed      = 0.5f;

		for (const auto& drawInfo : drawInfos)
		{
			WaterPassPushConstants pc = paramsBase;

			if (drawInfo.paramsIndex < nLakes)
			{
				// Lac : on lit les champs depuis LakeInstance.
				const auto& lake   = scene.lakes[drawInfo.paramsIndex];
				pc.bottomColor[0]     = lake.bottomColor.x;
				pc.bottomColor[1]     = lake.bottomColor.y;
				pc.bottomColor[2]     = lake.bottomColor.z;
				pc.turbidity          = lake.turbidity;
				pc.flowDirection[0]   = 0.0f;
				pc.flowDirection[1]   = 0.0f;
				pc.flowSpeed          = 0.0f;   // Lac : pas de flow directionnel
				pc.refractionAmount   = 0.02f;
				pc.fresnelPower       = 5.0f;
				pc.reflectionStrength = 0.5f;
			}
			else
			{
				// Rivière : défauts hardcodés + flowDirection (1,0) par convention
				// (le shader utilise vFlowDir interpolé depuis WaterVertex.flowDir).
				const uint32_t riverIdx = drawInfo.paramsIndex - nLakes;
				if (riverIdx >= static_cast<uint32_t>(scene.rivers.size())) continue;

				pc.bottomColor[0]     = kRiverDefaultBottomColor[0];
				pc.bottomColor[1]     = kRiverDefaultBottomColor[1];
				pc.bottomColor[2]     = kRiverDefaultBottomColor[2];
				pc.turbidity          = kRiverDefaultTurbidity;
				pc.flowDirection[0]   = 1.0f;
				pc.flowDirection[1]   = 0.0f;
				pc.flowSpeed          = kRiverDefaultFlowSpeed;
				pc.refractionAmount   = 0.015f;
				pc.fresnelPower       = 5.0f;
				pc.reflectionStrength = 0.4f;
			}

			vkCmdPushConstants(cmd, m_pipelineLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(WaterPassPushConstants), &pc);

			vkCmdDrawIndexed(cmd, drawInfo.indexCount, 1, drawInfo.firstIndex,
			                 drawInfo.vertexOffset, 0);
		}

		vkCmdEndRenderPass(cmd);
	}

} // namespace engine::render
