// Implementation SkyPass (Phase 5 Lunar + M38.1 Sky) — pipeline Vulkan
// fullscreen-quad pour le ciel + disque lunaire procedural.

#include "src/client/render/SkyPass.h"

#include <cstring>

namespace engine::render
{
	namespace
	{
		/// Cree un VkShaderModule a partir d'un SPIR-V deja charge en memoire.
		/// Renvoie VK_NULL_HANDLE en cas d'echec.
		/// \param device    Device Vulkan logique.
		/// \param code      Pointeur sur le SPIR-V (uint32 words).
		/// \param wordCount Nombre de uint32 dans \p code.
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t wordCount)
		{
			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode    = code;
			VkShaderModule m = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &info, nullptr, &m) != VK_SUCCESS)
				return VK_NULL_HANDLE;
			return m;
		}
	}

	bool SkyPass::Init(VkDevice device, VkRenderPass renderPass, uint32_t subpass,
	                    const uint32_t* vertSpirv, size_t vertWordCount,
	                    const uint32_t* fragSpirv, size_t fragWordCount)
	{
		// 1) Pipeline layout avec push-constants (144 bytes, vertex+fragment stages).
		VkPushConstantRange pcRange{};
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pcRange.offset     = 0;
		pcRange.size       = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo plInfo{};
		plInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plInfo.pushConstantRangeCount = 1;
		plInfo.pPushConstantRanges    = &pcRange;
		if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			return false;

		// 2) Shaders.
		VkShaderModule vertModule = CreateShaderModule(device, vertSpirv, vertWordCount);
		VkShaderModule fragModule = CreateShaderModule(device, fragSpirv, fragWordCount);
		if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
		{
			if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertModule, nullptr);
			if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragModule, nullptr);
			return false;
		}

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName  = "main";
		stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName  = "main";

		// 3) Vertex input vide (le fullscreen quad est genere via gl_VertexIndex).
		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// 4) Viewport / scissor dynamiques.
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

		// Concern 1 fix (Phase 5 Lunar PR #561 review) :
		// depthTestEnable=TRUE + depthCompareOp=LESS_OR_EQUAL + depthWriteEnable=FALSE.
		// Convention LCDLLN = standard Z (depth=1.0 = far) cf. GeometryPass /
		// ShadowMapPass / TerrainChunkPipeline / WaterPass qui utilisent tous
		// VK_COMPARE_OP_LESS_OR_EQUAL. sky.vert ecrit `gl_Position.z = 1.0` donc
		// le sky ne passe le test que la ou la depth a ete laissee a la clear
		// value 1.0 (= no geometry). Les pixels avec geometrie ont depth < 1.0,
		// le test `1.0 <= depth_geo` echoue et la geometrie reste intacte dans
		// GBuffer A. depthWriteEnable=FALSE car LightingPass detecte les pixels
		// "sky" via depth==1.0 ; on ne doit pas modifier la depth.
		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable  = VK_TRUE;
		ds.depthWriteEnable = VK_FALSE;
		ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

		// Concern 2 fix (Phase 5 Lunar PR #561 review) :
		// Le render pass parent (GeometryPass::GetRenderPassLoad) declare 4
		// color attachments (GBufferA/albedo, GBufferB/normal, GBufferC/ORM,
		// Velocity). Vulkan exige que `attachmentCount` corresponde exactement
		// au nombre de color attachments du subpass courant. On configure 4
		// VkPipelineColorBlendAttachmentState : seul le slot [0] (GBufferA
		// albedo) est ecrit avec RGBA ; les 3 autres ont colorWriteMask=0
		// (pas de write) -- la geometrie de la frame courante reste intacte
		// dans GBufferB/C/Velocity et LightingPass continue a y lire les
		// vraies normales/ORM/velocity quand il y a de la geometrie.
		VkPipelineColorBlendAttachmentState cba[4]{};
		cba[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		cba[1].colorWriteMask = 0;
		cba[2].colorWriteMask = 0;
		cba[3].colorWriteMask = 0;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 4;
		cb.pAttachments    = cba;

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
		pi.renderPass          = renderPass;
		pi.subpass             = subpass;

		const VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &m_pipeline);
		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
		return res == VK_SUCCESS;
	}

	void SkyPass::Shutdown(VkDevice device)
	{
		if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
		if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
		m_pipeline       = VK_NULL_HANDLE;
		m_pipelineLayout = VK_NULL_HANDLE;
	}

	void SkyPass::Record(VkCommandBuffer cmd, const PushConstants& pc)
	{
		if (m_pipeline == VK_NULL_HANDLE) return;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdPushConstants(cmd, m_pipelineLayout,
		                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		                    0, sizeof(PushConstants), &pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}
}
