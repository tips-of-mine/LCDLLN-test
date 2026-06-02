// src/client/render/ImpostorPass.cpp (M45.5)
//
// Implémentation de `ImpostorPass`. Voir ImpostorPass.h pour l'API. Le pipeline
// est calqué sur `TerrainChunkPipeline`/`GeometryPass` pour rester compatible
// avec le render pass GBuffer loadOp=LOAD (4 color + 1 depth) : même état MRT
// (4 blend attachments, blend OFF), depth test+write ON. Pas de vertex buffer :
// le quad billboard est généré dans le VS depuis gl_VertexIndex (6 sommets).

#include "src/client/render/ImpostorPass.h"

#include "src/client/render/PipelineCache.h"
#include "src/shared/core/Log.h"

namespace engine::render
{
	namespace
	{
		/// Crée un module shader Vulkan depuis un blob SPIR-V (mêmes conventions
		/// que TerrainChunkPipeline). \return VK_NULL_HANDLE en cas d'échec.
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* spirv,
			size_t wordCount, const char* tag)
		{
			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode    = spirv;
			VkShaderModule mod = VK_NULL_HANDLE;
			const VkResult r = vkCreateShaderModule(device, &info, nullptr, &mod);
			if (r != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[ImpostorPass] vkCreateShaderModule({}) failed: {}",
					tag, static_cast<int>(r));
				return VK_NULL_HANDLE;
			}
			return mod;
		}
	} // namespace

	bool ImpostorPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkRenderPass gbufferLoadRenderPass,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || gbufferLoadRenderPass == VK_NULL_HANDLE
			|| vertSpirv == nullptr || vertWordCount == 0
			|| fragSpirv == nullptr || fragWordCount == 0)
		{
			LOG_WARN(Render, "[ImpostorPass] Init: args invalides (device/renderPass/spirv null)");
			return false;
		}

		// 1) Descriptor set layout (set 0) : 3 combined image samplers
		//    (binding 0 = albedo, binding 1 = normal, binding 2 = orm), stage FRAGMENT.
		{
			VkDescriptorSetLayoutBinding bindings[3]{};
			for (uint32_t i = 0; i < 3u; ++i)
			{
				bindings[i].binding         = i;
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			VkDescriptorSetLayoutCreateInfo info{};
			info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 3;
			info.pBindings    = bindings;
			if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_setLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[ImpostorPass] vkCreateDescriptorSetLayout failed");
				Destroy(device);
				return false;
			}
		}

		// 2) Descriptor pool + anneau de kDescRing sets (un par atlas/draw, cf. .h).
		//    3 image samplers par set.
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 3u * kDescRing;
			VkDescriptorPoolCreateInfo pci{};
			pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pci.maxSets       = kDescRing;
			pci.poolSizeCount = 1;
			pci.pPoolSizes    = &poolSize;
			if (vkCreateDescriptorPool(device, &pci, nullptr, &m_descPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[ImpostorPass] vkCreateDescriptorPool failed");
				Destroy(device);
				return false;
			}
			VkDescriptorSetLayout layouts[kDescRing];
			for (uint32_t i = 0; i < kDescRing; ++i) layouts[i] = m_setLayout;
			VkDescriptorSetAllocateInfo dsai{};
			dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			dsai.descriptorPool     = m_descPool;
			dsai.descriptorSetCount = kDescRing;
			dsai.pSetLayouts        = layouts;
			if (vkAllocateDescriptorSets(device, &dsai, m_descSets) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[ImpostorPass] vkAllocateDescriptorSets failed");
				Destroy(device);
				return false;
			}
		}

		// 3) Sampler linéaire CLAMP_TO_EDGE (évite le bleeding entre tiles de l'atlas).
		{
			VkSamplerCreateInfo sci{};
			sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sci.magFilter    = VK_FILTER_LINEAR;
			sci.minFilter    = VK_FILTER_LINEAR;
			sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.maxLod       = 0.0f;
			if (vkCreateSampler(device, &sci, nullptr, &m_sampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[ImpostorPass] vkCreateSampler failed");
				Destroy(device);
				return false;
			}
		}

		// 4) Pipeline layout : set 0 + 1 push constant range (VERTEX+FRAGMENT).
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushRange.offset     = 0;
			pushRange.size       = static_cast<uint32_t>(sizeof(ImpostorPushConstants));

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount         = 1;
			layoutInfo.pSetLayouts            = &m_setLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushRange;
			if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[ImpostorPass] vkCreatePipelineLayout failed");
				Destroy(device);
				return false;
			}
		}

		// 5) Shader modules.
		VkShaderModule vertModule = CreateShaderModule(device, vertSpirv, vertWordCount, "impostor.vert");
		VkShaderModule fragModule = CreateShaderModule(device, fragSpirv, fragWordCount, "impostor.frag");
		if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
		{
			if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertModule, nullptr);
			if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragModule, nullptr);
			Destroy(device);
			return false;
		}

		// 6) État du pipeline graphique. Pas de vertex input (quad via gl_VertexIndex).
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName  = "main";
		stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName  = "main";

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount  = 1;

		// Billboard camera-facing : pas de winding cohérent garanti -> cull OFF
		// (le quad fait toujours face à la caméra ; le coût d'un cull erroné serait
		// un billboard invisible). Sûr et conservateur.
		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode    = VK_CULL_MODE_NONE;
		rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth   = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Depth test+write ON : les impostors s'intègrent au depth buffer comme les
		// props/terrain (occlusion correcte vis-à-vis du reste de la scène).
		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable  = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

		// 4 color blend attachments (albedo/normal/ORM/velocity), blend OFF, RGBA write.
		VkPipelineColorBlendAttachmentState blendAtt[4]{};
		for (int i = 0; i < 4; ++i)
		{
			blendAtt[i].blendEnable    = VK_FALSE;
			blendAtt[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
				| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		}
		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 4;
		cb.pAttachments    = blendAtt;

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
		gpInfo.renderPass          = gbufferLoadRenderPass;
		gpInfo.subpass             = 0;

		AssertPipelineCreationAllowed();
		const VkResult r = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpInfo, nullptr, &m_pipeline);
		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
		if (r != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[ImpostorPass] vkCreateGraphicsPipelines failed: {}", static_cast<int>(r));
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "[Boot] ImpostorPass init OK");
		return true;
	}

	void ImpostorPass::RecordInstances(VkDevice device, VkCommandBuffer cmd, VkExtent2D /*extent*/,
		const ImpostorInstance* instances, uint32_t count,
		VkImageView albedoView, VkSampler albedoSamp,
		VkImageView normalView, VkSampler normalSamp,
		VkImageView ormView, VkSampler ormSamp,
		const float* viewProj, const float* prevViewProj, const float* cameraPos3,
		uint32_t viewsPerAxis, uint32_t tileSize, float parallaxScale)
	{
		if (m_pipeline == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE || instances == nullptr
			|| count == 0 || albedoView == VK_NULL_HANDLE || normalView == VK_NULL_HANDLE
			|| ormView == VK_NULL_HANDLE
			|| albedoSamp == VK_NULL_HANDLE || normalSamp == VK_NULL_HANDLE
			|| ormSamp == VK_NULL_HANDLE
			|| viewProj == nullptr || cameraPos3 == nullptr)
		{
			return;
		}

		// Prend le prochain set de l'anneau (évite d'écraser un set encore
		// référencé par un draw précédent ou une frame en vol — cf. kDescRing).
		VkDescriptorSet descSet = m_descSets[m_descCursor];
		m_descCursor = (m_descCursor + 1u) % kDescRing;

		// Met à jour CE set avec l'atlas courant (3 samplers : albedo, normal, orm).
		VkDescriptorImageInfo imgInfos[3]{};
		imgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imgInfos[0].imageView   = albedoView;
		imgInfos[0].sampler     = albedoSamp;
		imgInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imgInfos[1].imageView   = normalView;
		imgInfos[1].sampler     = normalSamp;
		imgInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imgInfos[2].imageView   = ormView;
		imgInfos[2].sampler     = ormSamp;
		VkWriteDescriptorSet writes[3]{};
		for (uint32_t i = 0; i < 3u; ++i)
		{
			writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet          = descSet;
			writes[i].dstBinding      = i;
			writes[i].descriptorCount = 1u;
			writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo      = &imgInfos[i];
		}
		vkUpdateDescriptorSets(device, 3u, writes, 0, nullptr);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_pipelineLayout, 0, 1, &descSet, 0, nullptr);

		// Push constants : partie commune (matrices/caméra/atlas) constante pour
		// toutes les instances de cet atlas ; seule instancePos change par draw.
		ImpostorPushConstants pc{};
		for (int i = 0; i < 16; ++i) pc.viewProj[i] = viewProj[i];
		if (prevViewProj != nullptr) { for (int i = 0; i < 16; ++i) pc.prevViewProj[i] = prevViewProj[i]; }
		else                          { for (int i = 0; i < 16; ++i) pc.prevViewProj[i] = viewProj[i]; }
		pc.cameraPos[0] = cameraPos3[0];
		pc.cameraPos[1] = cameraPos3[1];
		pc.cameraPos[2] = cameraPos3[2];
		pc.cameraPos[3] = 0.0f;
		pc.atlasParams[0] = static_cast<float>(viewsPerAxis);
		pc.atlasParams[1] = static_cast<float>(tileSize);
		pc.atlasParams[2] = 1.0f;            // fadeAlpha (v1 : pas de fondu, opaque)
		pc.atlasParams[3] = parallaxScale;   // v2 : échelle du décalage de parallax (frag)

		for (uint32_t i = 0; i < count; ++i)
		{
			pc.instancePos[0] = instances[i].worldPos[0];
			pc.instancePos[1] = instances[i].worldPos[1];
			pc.instancePos[2] = instances[i].worldPos[2];
			pc.instancePos[3] = instances[i].radius;
			vkCmdPushConstants(cmd, m_pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				sizeof(ImpostorPushConstants), &pc);
			vkCmdDraw(cmd, 6, 1, 0, 0);
		}
	}

	void ImpostorPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
		if (m_pipeline != VK_NULL_HANDLE)       { vkDestroyPipeline(device, m_pipeline, nullptr);             m_pipeline = VK_NULL_HANDLE; }
		if (m_pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
		if (m_sampler != VK_NULL_HANDLE)        { vkDestroySampler(device, m_sampler, nullptr);               m_sampler = VK_NULL_HANDLE; }
		// Le pool libère implicitement tout l'anneau m_descSets.
		if (m_descPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_descPool, nullptr);
			m_descPool = VK_NULL_HANDLE;
			for (uint32_t i = 0; i < kDescRing; ++i) m_descSets[i] = VK_NULL_HANDLE;
			m_descCursor = 0;
		}
		if (m_setLayout != VK_NULL_HANDLE)      { vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr); m_setLayout = VK_NULL_HANDLE; }
	}
}
