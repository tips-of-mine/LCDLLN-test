#include "src/client/render/CloudPass.h"
#include "src/client/render/PipelineCache.h"
#include "src/client/render/clouds/CloudNoiseGenerator.h"
#include "src/shared/core/Log.h"

#include <array>
#include <chrono>
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

		/// Trouve un type mémoire compatible (pattern standard Vulkan).
		/// \return l'index du type, ou UINT32_MAX si aucun ne convient.
		uint32_t FindMemoryType(VkPhysicalDevice physicalDevice,
			uint32_t typeBits, VkMemoryPropertyFlags props)
		{
			VkPhysicalDeviceMemoryProperties memProps{};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((typeBits & (1u << i))
					&& (memProps.memoryTypes[i].propertyFlags & props) == props)
				{
					return i;
				}
			}
			return UINT32_MAX;
		}
	}

	bool CloudPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		const uint32_t* compositeFragSpirv, size_t compositeFragWordCount,
		VkQueue uploadQueue, uint32_t uploadQueueFamilyIndex,
		uint32_t maxFrames, VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
			|| vertWordCount == 0 || fragWordCount == 0
			|| !compositeFragSpirv || compositeFragWordCount == 0
			|| uploadQueue == VK_NULL_HANDLE)
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

		// 2. Descriptor set layout : 4 combined image samplers (scene color,
		// depth, bruit base 3D, bruit détail 3D — chantier ciel 2026-07-17).
		{
			std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
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

		// 2b. Lot 1 (2026-07-18) — layout du set de composition : 2 samplers
		// (scene pleine résolution + nuages basse résolution).
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
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_compositeSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateDescriptorSetLayout (composite) failed");
				Destroy(device); return false;
			}
		}

		// 3. Descriptor pool (marche : 4 bindings + composite : 2 bindings,
		// un set de chaque par frame).
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 6 * m_maxFrames;
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = 2 * m_maxFrames;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateDescriptorPool failed");
				Destroy(device); return false;
			}
		}

		// 4. Alloue les descriptor sets (marche + composite) par frame.
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
			std::vector<VkDescriptorSetLayout> compLayouts(m_maxFrames, m_compositeSetLayout);
			allocInfo.pSetLayouts = compLayouts.data();
			m_compositeSets.resize(m_maxFrames, VK_NULL_HANDLE);
			if (vkAllocateDescriptorSets(device, &allocInfo, m_compositeSets.data()) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkAllocateDescriptorSets (composite) failed");
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
			// Sampler des textures de bruit : linéaire + REPEAT (les bruits
			// sont périodiques, le tuilage doit être sans couture).
			si.magFilter    = VK_FILTER_LINEAR;
			si.minFilter    = VK_FILTER_LINEAR;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			if (vkCreateSampler(device, &si, nullptr, &m_noiseSampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateSampler (noise repeat) failed");
				Destroy(device); return false;
			}
		}

		// 5b. Chantier ciel 2026-07-17 — génération CPU + upload des textures
		// 3D de bruit Perlin-Worley (base 64³ + détail 32³). One-shot au boot
		// via un command pool transitoire (vkQueueWaitIdle interne).
		{
			const auto t0 = std::chrono::steady_clock::now();
			const clouds::CloudNoiseData noise = clouds::GenerateCloudNoise(1337u);

			VkCommandPool uploadPool = VK_NULL_HANDLE;
			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			poolInfo.queueFamilyIndex = uploadQueueFamilyIndex;
			if (vkCreateCommandPool(device, &poolInfo, nullptr, &uploadPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateCommandPool (noise upload) failed");
				Destroy(device); return false;
			}

			const bool okBase = CreateNoiseTexture3D(device, physicalDevice,
				uploadPool, uploadQueue, clouds::kBaseNoiseSize,
				noise.baseRgba.data(),
				m_noiseBaseImage, m_noiseBaseMemory, m_noiseBaseView);
			const bool okDetail = okBase && CreateNoiseTexture3D(device, physicalDevice,
				uploadPool, uploadQueue, clouds::kDetailNoiseSize,
				noise.detailRgba.data(),
				m_noiseDetailImage, m_noiseDetailMemory, m_noiseDetailView);
			vkDestroyCommandPool(device, uploadPool, nullptr);
			if (!okBase || !okDetail)
			{
				LOG_ERROR(Render, "CloudPass: creation des textures de bruit 3D echouee");
				Destroy(device); return false;
			}
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - t0).count();
			LOG_INFO(Render, "CloudPass: bruit Perlin-Worley genere+uploade en {} ms "
				"(base {}^3, detail {}^3)", static_cast<long long>(ms),
				clouds::kBaseNoiseSize, clouds::kDetailNoiseSize);
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

			// Lot 1 — layout du pipeline de composition (set composite, pas
			// de push constants).
			VkPipelineLayoutCreateInfo compLayoutInfo{};
			compLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			compLayoutInfo.setLayoutCount = 1;
			compLayoutInfo.pSetLayouts    = &m_compositeSetLayout;
			if (vkCreatePipelineLayout(device, &compLayoutInfo, nullptr, &m_compositePipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreatePipelineLayout (composite) failed");
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
			if (res != VK_SUCCESS)
			{
				vkDestroyShaderModule(device, vertMod, nullptr);
				vkDestroyShaderModule(device, fragMod, nullptr);
				LOG_ERROR(Render, "CloudPass: vkCreateGraphicsPipelines failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}

			// Lot 1 (2026-07-18) — pipeline de composition : mêmes états
			// (fullscreen triangle, pas de depth, pas de blend — l'équation
			// de composition est dans le shader), fragment
			// clouds_composite.frag, layout composite. Même render pass
			// (format RGBA16F identique pour la cible réduite et la scène).
			VkShaderModule compFragMod = CreateShaderModule(device,
				compositeFragSpirv, compositeFragWordCount);
			if (compFragMod == VK_NULL_HANDLE)
			{
				vkDestroyShaderModule(device, vertMod, nullptr);
				vkDestroyShaderModule(device, fragMod, nullptr);
				LOG_ERROR(Render, "CloudPass: composite shader module creation failed");
				Destroy(device); return false;
			}
			stages[1].module = compFragMod;
			gpInfo.layout    = m_compositePipelineLayout;
			AssertPipelineCreationAllowed();
			PipelineCache::RegisterWarmupKey(HashGraphicsPsoKey(m_renderPass, 0, m_compositePipelineLayout, sceneColorHDRFormat, VK_FORMAT_UNDEFINED));
			res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpInfo, nullptr, &m_compositePipeline);
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			vkDestroyShaderModule(device, compFragMod, nullptr);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateGraphicsPipelines (composite) failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		LOG_INFO(Render, "CloudPass: initialized (maxFrames={})", m_maxFrames);
		return true;
	}

	void CloudPass::RecordMarch(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D scaledExtent, ResourceId idDepth,
		ResourceId idCloudsOut, const CloudPushConstants& params, uint32_t frameIndex)
	{
		const VkExtent2D extent = scaledExtent;
		if (!IsValid() || extent.width == 0 || extent.height == 0) return;

		VkImageView viewDepth = registry.getImageView(idDepth);
		VkImageView viewOut   = registry.getImageView(idCloudsOut);
		if (viewDepth == VK_NULL_HANDLE || viewOut == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "CloudPass::RecordMarch: missing image views, skipping");
			return;
		}

		const uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		// Binding 0 (ex-scene color) : plus échantillonné par clouds.frag
		// (lot 1) mais toujours présent au layout — on y lie le depth en
		// « bouche-trou » déclaré au FrameGraph (layout SHADER_READ_ONLY).
		std::array<VkDescriptorImageInfo, 4> imageInfos{};
		imageInfos[0] = { m_nearestSampler, viewDepth,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_nearestSampler, viewDepth,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[2] = { m_noiseSampler,   m_noiseBaseView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[3] = { m_noiseSampler,   m_noiseDetailView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		std::array<VkWriteDescriptorSet, 4> writes{};
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

	void CloudPass::RecordComposite(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent, ResourceId idSceneColorIn, ResourceId idCloudsIn,
		ResourceId idSceneColorOut, uint32_t frameIndex)
	{
		if (!IsValid() || m_compositePipeline == VK_NULL_HANDLE
			|| extent.width == 0 || extent.height == 0)
		{
			return;
		}

		VkImageView viewSceneIn = registry.getImageView(idSceneColorIn);
		VkImageView viewClouds  = registry.getImageView(idCloudsIn);
		VkImageView viewOut     = registry.getImageView(idSceneColorOut);
		if (viewSceneIn == VK_NULL_HANDLE || viewClouds == VK_NULL_HANDLE
			|| viewOut == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "CloudPass::RecordComposite: missing image views, skipping");
			return;
		}

		const uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_compositeSets[setIdx];

		std::array<VkDescriptorImageInfo, 2> imageInfos{};
		imageInfos[0] = { m_linearSampler, viewSceneIn, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_linearSampler, viewClouds,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
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

		// Framebuffer mis en cache (même pattern que la marche : la clé
		// inclut la vue de sortie et la taille).
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
				LOG_ERROR(Render, "CloudPass::RecordComposite: vkCreateFramebuffer failed");
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

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_compositePipelineLayout, 0, 1, &ds, 0, nullptr);

		VkViewport viewport{};
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd);
	}

	bool CloudPass::CreateNoiseTexture3D(VkDevice device, VkPhysicalDevice physicalDevice,
		VkCommandPool cmdPool, VkQueue queue,
		int size, const uint8_t* rgba,
		VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView)
	{
		const VkDeviceSize byteSize =
			static_cast<VkDeviceSize>(size) * size * size * 4u;

		// 1. Image 3D device-local.
		{
			VkImageCreateInfo ii{};
			ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ii.imageType     = VK_IMAGE_TYPE_3D;
			ii.format        = VK_FORMAT_R8G8B8A8_UNORM;
			ii.extent        = { static_cast<uint32_t>(size),
			                     static_cast<uint32_t>(size),
			                     static_cast<uint32_t>(size) };
			ii.mipLevels     = 1;
			ii.arrayLayers   = 1;
			ii.samples       = VK_SAMPLE_COUNT_1_BIT;
			ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
			ii.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (vkCreateImage(device, &ii, nullptr, &outImage) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateImage (bruit 3D {}^3) failed", size);
				return false;
			}
			VkMemoryRequirements req{};
			vkGetImageMemoryRequirements(device, outImage, &req);
			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = FindMemoryType(physicalDevice, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (ai.memoryTypeIndex == UINT32_MAX
				|| vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS
				|| vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: allocation memoire image bruit 3D failed");
				return false;
			}
		}

		// 2. Staging host-visible + copie CPU.
		VkBuffer stagingBuf = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		{
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = byteSize;
			bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateBuffer(device, &bi, nullptr, &stagingBuf) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateBuffer (staging bruit) failed");
				return false;
			}
			VkMemoryRequirements req{};
			vkGetBufferMemoryRequirements(device, stagingBuf, &req);
			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = FindMemoryType(physicalDevice, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (ai.memoryTypeIndex == UINT32_MAX
				|| vkAllocateMemory(device, &ai, nullptr, &stagingMem) != VK_SUCCESS
				|| vkBindBufferMemory(device, stagingBuf, stagingMem, 0) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: allocation staging bruit failed");
				if (stagingBuf) vkDestroyBuffer(device, stagingBuf, nullptr);
				if (stagingMem) vkFreeMemory(device, stagingMem, nullptr);
				return false;
			}
			void* mapped = nullptr;
			if (vkMapMemory(device, stagingMem, 0, byteSize, 0, &mapped) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkMapMemory (staging bruit) failed");
				vkDestroyBuffer(device, stagingBuf, nullptr);
				vkFreeMemory(device, stagingMem, nullptr);
				return false;
			}
			std::memcpy(mapped, rgba, static_cast<size_t>(byteSize));
			vkUnmapMemory(device, stagingMem);
		}

		// 3. Command buffer one-shot : UNDEFINED → TRANSFER_DST, copie,
		// → SHADER_READ_ONLY (fragment). Submit + wait (boot uniquement).
		bool ok = false;
		{
			VkCommandBufferAllocateInfo cbai{};
			cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cbai.commandPool        = cmdPool;
			cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cbai.commandBufferCount = 1;
			VkCommandBuffer cmd = VK_NULL_HANDLE;
			if (vkAllocateCommandBuffers(device, &cbai, &cmd) == VK_SUCCESS)
			{
				VkCommandBufferBeginInfo bi{};
				bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				vkBeginCommandBuffer(cmd, &bi);

				VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				VkImageMemoryBarrier toDst{};
				toDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toDst.srcAccessMask       = 0;
				toDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
				toDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
				toDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toDst.image               = outImage;
				toDst.subresourceRange    = range;
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

				VkBufferImageCopy copy{};
				copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				copy.imageExtent      = { static_cast<uint32_t>(size),
				                          static_cast<uint32_t>(size),
				                          static_cast<uint32_t>(size) };
				vkCmdCopyBufferToImage(cmd, stagingBuf, outImage,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

				VkImageMemoryBarrier toRead = toDst;
				toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				toRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);

				vkEndCommandBuffer(cmd);
				VkSubmitInfo si{};
				si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.commandBufferCount = 1;
				si.pCommandBuffers    = &cmd;
				if (vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS)
				{
					vkQueueWaitIdle(queue);
					ok = true;
				}
				else
				{
					LOG_ERROR(Render, "CloudPass: vkQueueSubmit (upload bruit) failed");
				}
				vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
			}
			else
			{
				LOG_ERROR(Render, "CloudPass: vkAllocateCommandBuffers (upload bruit) failed");
			}
		}
		vkDestroyBuffer(device, stagingBuf, nullptr);
		vkFreeMemory(device, stagingMem, nullptr);
		if (!ok) return false;

		// 4. Vue 3D.
		{
			VkImageViewCreateInfo vi{};
			vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			vi.image            = outImage;
			vi.viewType         = VK_IMAGE_VIEW_TYPE_3D;
			vi.format           = VK_FORMAT_R8G8B8A8_UNORM;
			vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			if (vkCreateImageView(device, &vi, nullptr, &outView) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateImageView (bruit 3D) failed");
				return false;
			}
		}
		return true;
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
		// Lot 1 — pipeline de composition.
		if (m_compositePipeline)       { vkDestroyPipeline(device, m_compositePipeline, nullptr); m_compositePipeline = VK_NULL_HANDLE; }
		if (m_compositePipelineLayout) { vkDestroyPipelineLayout(device, m_compositePipelineLayout, nullptr); m_compositePipelineLayout = VK_NULL_HANDLE; }
		m_compositeSets.clear();
		if (m_compositeSetLayout)      { vkDestroyDescriptorSetLayout(device, m_compositeSetLayout, nullptr); m_compositeSetLayout = VK_NULL_HANDLE; }
		if (m_nearestSampler)      { vkDestroySampler(device, m_nearestSampler, nullptr); m_nearestSampler = VK_NULL_HANDLE; }
		if (m_linearSampler)       { vkDestroySampler(device, m_linearSampler, nullptr); m_linearSampler = VK_NULL_HANDLE; }
		// Chantier ciel 2026-07-17 — textures 3D de bruit + sampler REPEAT.
		if (m_noiseSampler)        { vkDestroySampler(device, m_noiseSampler, nullptr); m_noiseSampler = VK_NULL_HANDLE; }
		if (m_noiseBaseView)       { vkDestroyImageView(device, m_noiseBaseView, nullptr); m_noiseBaseView = VK_NULL_HANDLE; }
		if (m_noiseBaseImage)      { vkDestroyImage(device, m_noiseBaseImage, nullptr); m_noiseBaseImage = VK_NULL_HANDLE; }
		if (m_noiseBaseMemory)     { vkFreeMemory(device, m_noiseBaseMemory, nullptr); m_noiseBaseMemory = VK_NULL_HANDLE; }
		if (m_noiseDetailView)     { vkDestroyImageView(device, m_noiseDetailView, nullptr); m_noiseDetailView = VK_NULL_HANDLE; }
		if (m_noiseDetailImage)    { vkDestroyImage(device, m_noiseDetailImage, nullptr); m_noiseDetailImage = VK_NULL_HANDLE; }
		if (m_noiseDetailMemory)   { vkFreeMemory(device, m_noiseDetailMemory, nullptr); m_noiseDetailMemory = VK_NULL_HANDLE; }
		m_descriptorSets.clear();
		if (m_descriptorPool)      { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
		if (m_descriptorSetLayout) { vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
		if (m_renderPass)          { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
		LOG_INFO(Render, "[CloudPass] Destroyed");
	}
}
