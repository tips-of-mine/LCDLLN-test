// engine/render/TerrainChunkPipeline.cpp (M100.9 — Task 13)
//
// Implémentation du pipeline graphique terrain_chunk. Voir
// `TerrainChunkPipeline.h` pour la documentation API. Ce fichier se concentre
// sur le boilerplate Vulkan minimal compatible avec le render pass GBuffer
// existant (4 color + 1 depth, formats fournis par l'appelant via le
// `renderPass`).

#include "engine/render/TerrainChunkPipeline.h"

#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"
#include "engine/world/terrain/TerrainMeshBuilder.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace engine::render
{
	namespace
	{
		/// Push constant : vec3 chunkOriginWorld + 1 float pad (alignement vec4).
		/// Doit matcher `terrain_chunk.vert` : `layout(push_constant) uniform PushConstants`.
		constexpr uint32_t kPushConstantSize = 16u;

		struct ChunkPushConstants
		{
			float chunkOriginWorld[3]{};
			float pad0 = 0.0f;
		};
		static_assert(sizeof(ChunkPushConstants) == kPushConstantSize,
			"ChunkPushConstants must stay 16 bytes (vec3 + pad)");

		/// Crée un module shader Vulkan depuis un blob SPIR-V.
		/// \return VK_NULL_HANDLE en cas d'échec (et log erreur).
		VkShaderModule CreateShaderModule(VkDevice device,
			const uint32_t* spirv, size_t wordCount, const char* tag)
		{
			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode    = spirv;
			VkShaderModule mod = VK_NULL_HANDLE;
			const VkResult r = vkCreateShaderModule(device, &info, nullptr, &mod);
			if (r != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkPipeline] vkCreateShaderModule({}) failed: {}",
					tag, static_cast<int>(r));
				return VK_NULL_HANDLE;
			}
			return mod;
		}

		/// Charge un fichier SPIR-V `.spv` précompilé depuis disque (pattern
		/// utilisé par tous les autres pipelines : cf. DeferredPipeline.cpp,
		/// `loadSpirv`). Évite la dépendance runtime à `glslangValidator` —
		/// le `.spv` est produit au build par `tools/compile_game_shaders.ps1`.
		/// \return false si lecture échouée (et `outError` rempli).
		bool LoadSpirv(const std::filesystem::path& spvPath,
			std::vector<uint32_t>& outSpirv, std::string& outError)
		{
			std::ifstream f(spvPath, std::ios::binary | std::ios::ate);
			if (!f.is_open())
			{
				outError = "SPIR-V file not found: '" + spvPath.string()
					+ "' (build step compile_game_shaders.ps1 doit avoir produit ce fichier)";
				return false;
			}
			const std::streamsize size = f.tellg();
			if (size <= 0 || (size % 4) != 0)
			{
				outError = "SPIR-V file empty or not aligned to 4 bytes: '" + spvPath.string() + "'";
				return false;
			}
			f.seekg(0, std::ios::beg);
			outSpirv.resize(static_cast<size_t>(size) / 4u);
			f.read(reinterpret_cast<char*>(outSpirv.data()), size);
			return true;
		}
	}

	bool TerrainChunkPipeline::Init(VkDevice device, VkPhysicalDevice /*physDev*/,
		VkRenderPass renderPass,
		VkDescriptorSetLayout cameraSetLayout,
		const std::string& shaderRootPath,
		std::string& outError)
	{
		if (device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE
			|| cameraSetLayout == VK_NULL_HANDLE)
		{
			outError = "TerrainChunkPipeline::Init invalid args (device/renderPass/cameraSetLayout null)";
			return false;
		}

		// ---------------------------------------------------------------------
		// 1) Chargement des SPIR-V précompilés (pattern DeferredPipeline).
		//    Les .spv sont produits par tools/compile_game_shaders.ps1 au build.
		// ---------------------------------------------------------------------
		std::filesystem::path vertPath = std::filesystem::path(shaderRootPath) / "terrain_chunk.vert.spv";
		std::filesystem::path fragPath = std::filesystem::path(shaderRootPath) / "terrain_chunk.frag.spv";

		std::vector<uint32_t> vertSpirv;
		std::vector<uint32_t> fragSpirv;
		if (!LoadSpirv(vertPath, vertSpirv, outError))
			return false;
		if (!LoadSpirv(fragPath, fragSpirv, outError))
			return false;

		// ---------------------------------------------------------------------
		// 2) Descriptor set layouts.
		//    - Set 0 : caméra (fourni par le caller).
		//    - Set 1 : layout vide (Vulkan exige des sets contigus si on bind
		//              le set 2 — on crée un layout sans binding).
		//    - Set 2 : splat resources (6 bindings, voir header).
		// ---------------------------------------------------------------------
		// Set 1 vide.
		{
			VkDescriptorSetLayoutCreateInfo info{};
			info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 0;
			info.pBindings    = nullptr;
			const VkResult r = vkCreateDescriptorSetLayout(device, &info, nullptr, &m_emptySet1Layout);
			if (r != VK_SUCCESS)
			{
				outError = "vkCreateDescriptorSetLayout(set1 empty) failed: " + std::to_string(r);
				Shutdown(device);
				return false;
			}
		}

		// Set 2 splat (6 bindings).
		{
			VkDescriptorSetLayoutBinding bindings[6]{};
			// (0) sampler2D u_splatMap0
			bindings[0].binding         = 0;
			bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[0].descriptorCount = 1;
			bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			// (1) sampler2D u_splatMap1
			bindings[1].binding         = 1;
			bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[1].descriptorCount = 1;
			bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			// (2) sampler2DArray u_albedoArray
			bindings[2].binding         = 2;
			bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[2].descriptorCount = 1;
			bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			// (3) sampler2DArray u_normalArray
			bindings[3].binding         = 3;
			bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[3].descriptorCount = 1;
			bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			// (4) sampler2DArray u_armArray
			bindings[4].binding         = 4;
			bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[4].descriptorCount = 1;
			bindings[4].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			// (5) UBO LayerParams
			bindings[5].binding         = 5;
			bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[5].descriptorCount = 1;
			bindings[5].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 6;
			info.pBindings    = bindings;
			const VkResult r = vkCreateDescriptorSetLayout(device, &info, nullptr, &m_splatSetLayout);
			if (r != VK_SUCCESS)
			{
				outError = "vkCreateDescriptorSetLayout(set2 splat) failed: " + std::to_string(r);
				Shutdown(device);
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// 3) Pipeline layout : 3 set layouts + 1 push constant range.
		// ---------------------------------------------------------------------
		{
			VkDescriptorSetLayout setLayouts[3] = {
				cameraSetLayout,
				m_emptySet1Layout,
				m_splatSetLayout
			};

			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushRange.offset     = 0;
			pushRange.size       = kPushConstantSize;

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount         = 3;
			layoutInfo.pSetLayouts            = setLayouts;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushRange;

			const VkResult r = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
			if (r != VK_SUCCESS)
			{
				outError = "vkCreatePipelineLayout(terrain_chunk) failed: " + std::to_string(r);
				Shutdown(device);
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// 4) Shader modules.
		// ---------------------------------------------------------------------
		VkShaderModule vertModule = CreateShaderModule(device, vertSpirv.data(), vertSpirv.size(),
			"terrain_chunk.vert");
		VkShaderModule fragModule = CreateShaderModule(device, fragSpirv.data(), fragSpirv.size(),
			"terrain_chunk.frag");
		if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
		{
			if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertModule, nullptr);
			if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragModule, nullptr);
			outError = "Shader module creation failed (terrain_chunk)";
			Shutdown(device);
			return false;
		}

		// ---------------------------------------------------------------------
		// 5) Vertex input : 1 binding (32 octets), 3 attributs.
		// ---------------------------------------------------------------------
		VkVertexInputBindingDescription binding{};
		binding.binding   = 0;
		binding.stride    = static_cast<uint32_t>(sizeof(engine::world::terrain::TerrainVertex));
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attrs[3]{};
		attrs[0].location = 0;
		attrs[0].binding  = 0;
		attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[0].offset   = 0;
		attrs[1].location = 1;
		attrs[1].binding  = 0;
		attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[1].offset   = 12;
		attrs[2].location = 2;
		attrs[2].binding  = 0;
		attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;
		attrs[2].offset   = 24;

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount   = 1;
		vi.pVertexBindingDescriptions      = &binding;
		vi.vertexAttributeDescriptionCount = 3;
		vi.pVertexAttributeDescriptions    = attrs;

		// ---------------------------------------------------------------------
		// 6) Reste du pipeline graphique (topology, raster, blend, depth, etc.).
		// ---------------------------------------------------------------------
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName  = "main";
		stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName  = "main";

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
		rs.cullMode    = VK_CULL_MODE_BACK_BIT;
		// Cf. GeometryPass.cpp:385 : suite à la convention LH +Z forward (commit
		// ee181da), les meshes CCW world-space deviennent CW en clip space ;
		// on garde donc `frontFace = CW` pour ne pas culler tous les triangles.
		rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
		rs.lineWidth   = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable  = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

		// 4 color blend attachments : RGBA write pour albedo/normal/arm,
		// colorWriteMask=0 pour velocity (le shader n'écrit pas, mais le
		// render pass GBuffer en a 4 — on doit matcher l'attachmentCount).
		VkPipelineColorBlendAttachmentState blendAtt[4]{};
		for (int i = 0; i < 3; ++i)
		{
			blendAtt[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
				| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		}
		blendAtt[3].colorWriteMask = 0; // velocity — non écrit par terrain_chunk.frag

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
		gpInfo.renderPass          = renderPass;
		gpInfo.subpass             = 0;

		AssertPipelineCreationAllowed();
		// Pas de PipelineCache::RegisterWarmupKey ici : le caller (Engine boot)
		// est responsable de positionner la phase warmup avant l'appel.
		const VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline);
		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
		if (r != VK_SUCCESS)
		{
			outError = "vkCreateGraphicsPipelines(terrain_chunk) failed: " + std::to_string(r);
			Shutdown(device);
			return false;
		}

		LOG_INFO(Render, "[Boot] TerrainChunkPipeline init OK");
		return true;
	}

	void TerrainChunkPipeline::Shutdown(VkDevice device)
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
		if (m_splatSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_splatSetLayout, nullptr);
			m_splatSetLayout = VK_NULL_HANDLE;
		}
		if (m_emptySet1Layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_emptySet1Layout, nullptr);
			m_emptySet1Layout = VK_NULL_HANDLE;
		}
	}

	void TerrainChunkPipeline::RecordChunkDraw(VkCommandBuffer cmd,
		VkDescriptorSet cameraSet, VkDescriptorSet splatSet,
		const engine::world::terrain::TerrainMeshGpu& mesh,
		float chunkOriginX, float chunkOriginY, float chunkOriginZ)
	{
		if (m_pipeline == VK_NULL_HANDLE
			|| mesh.vertexBuffer == VK_NULL_HANDLE
			|| mesh.indexBuffer  == VK_NULL_HANDLE
			|| mesh.indexCount == 0)
		{
			return;
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

		// Bind set 0 (caméra). Le caller doit avoir alloué + écrit cet UBO.
		if (cameraSet != VK_NULL_HANDLE)
		{
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_pipelineLayout, 0, 1, &cameraSet, 0, nullptr);
		}
		// Bind set 2 (splat). Le set 1 est volontairement vide (pas de bind).
		if (splatSet != VK_NULL_HANDLE)
		{
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_pipelineLayout, 2, 1, &splatSet, 0, nullptr);
		}

		// Push constant : chunkOriginWorld (vec3 + pad).
		ChunkPushConstants pc{};
		pc.chunkOriginWorld[0] = chunkOriginX;
		pc.chunkOriginWorld[1] = chunkOriginY;
		pc.chunkOriginWorld[2] = chunkOriginZ;
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
			kPushConstantSize, &pc);

		// Bind vertex + index puis draw.
		const VkDeviceSize vbOffset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &vbOffset);
		vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
	}
}
