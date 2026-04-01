#include "engine/render/LightingPass.h"
#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace engine::render
{
	// -------------------------------------------------------------------------
	// Internal helpers
	// -------------------------------------------------------------------------

	namespace
	{
		/// Creates a VkShaderModule from SPIR-V words.
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

		/// Computes the inverse of a 4x4 column-major matrix using Cramer's rule.
		/// \param m   Input matrix (16 floats, column-major).
		/// \param out Output inverse (16 floats, column-major).
		/// \return false if the matrix is singular (determinant ~ 0).
		bool Mat4Inverse(const float* m, float* out)
		{
			// Using the 2x2 sub-determinants approach (standard cofactor method).
			// Column-major: element at row r, col c is m[c*4 + r].
			const float a00 = m[0], a10 = m[1], a20 = m[2], a30 = m[3];
			const float a01 = m[4], a11 = m[5], a21 = m[6], a31 = m[7];
			const float a02 = m[8], a12 = m[9], a22 = m[10], a32 = m[11];
			const float a03 = m[12], a13 = m[13], a23 = m[14], a33 = m[15];

			const float b00 = a00 * a11 - a10 * a01;
			const float b01 = a00 * a21 - a20 * a01;
			const float b02 = a00 * a31 - a30 * a01;
			const float b03 = a10 * a21 - a20 * a11;
			const float b04 = a10 * a31 - a30 * a11;
			const float b05 = a20 * a31 - a30 * a21;
			const float b06 = a02 * a13 - a12 * a03;
			const float b07 = a02 * a23 - a22 * a03;
			const float b08 = a02 * a33 - a32 * a03;
			const float b09 = a12 * a23 - a22 * a13;
			const float b10 = a12 * a33 - a32 * a13;
			const float b11 = a22 * a33 - a32 * a23;

			const float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
			if (det > -1e-7f && det < 1e-7f)
				return false; // singular

			const float inv = 1.0f / det;

			out[0]  = ( a11 * b11 - a21 * b10 + a31 * b09) * inv;
			out[1]  = (-a10 * b11 + a20 * b10 - a30 * b09) * inv;
			out[2]  = ( a13 * b05 - a23 * b04 + a33 * b03) * inv;
			out[3]  = (-a12 * b05 + a22 * b04 - a32 * b03) * inv;
			out[4]  = (-a01 * b11 + a21 * b08 - a31 * b07) * inv;
			out[5]  = ( a00 * b11 - a20 * b08 + a30 * b07) * inv;
			out[6]  = (-a03 * b05 + a23 * b02 - a33 * b01) * inv;
			out[7]  = ( a02 * b05 - a22 * b02 + a32 * b01) * inv;
			out[8]  = ( a01 * b10 - a11 * b08 + a31 * b06) * inv;
			out[9]  = (-a00 * b10 + a10 * b08 - a30 * b06) * inv;
			out[10] = ( a03 * b04 - a13 * b02 + a33 * b00) * inv;
			out[11] = (-a02 * b04 + a12 * b02 - a32 * b00) * inv;
			out[12] = (-a01 * b09 + a11 * b07 - a21 * b06) * inv;
			out[13] = ( a00 * b09 - a10 * b07 + a20 * b06) * inv;
			out[14] = (-a03 * b03 + a13 * b01 - a23 * b00) * inv;
			out[15] = ( a02 * b03 - a12 * b01 + a22 * b00) * inv;

			return true;
		}

	} // anonymous namespace

	// -------------------------------------------------------------------------
	// LightingPass::Init
	// -------------------------------------------------------------------------

	bool LightingPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxFrames,
		VkPipelineCache pipelineCache)
	{
		LOG_INFO(Render, "[LIGHT] Init enter");
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
			|| vertWordCount == 0 || fragWordCount == 0)
		{
			LOG_ERROR(Render, "LightingPass::Init: invalid arguments");
			return false;
		}

		m_maxFrames = maxFrames > 0 ? maxFrames : 1;

		// -----------------------------------------------------------------
		// 1. Render pass: 1 color attachment (SceneColor_HDR)
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

			// Dependency: wait for GBuffer writes (fragment + early fragment) before reading in fragment.
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
			LOG_INFO(Render, "[LIGHT] vkCreateRenderPass r={}", (int)res);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "LightingPass: vkCreateRenderPass failed: {}", static_cast<int>(res));
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 2. Descriptor set layout: 9 combined image samplers (GBufA/B/C, Depth, irradiance, prefilter, BRDF LUT, SSAO_Blur, DecalOverlay)
		// -----------------------------------------------------------------
		{
			std::array<VkDescriptorSetLayoutBinding, 9> bindings{};
			for (size_t i = 0; i < bindings.size(); ++i)
			{
				bindings[i].binding            = i;
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
				LOG_ERROR(Render, "LightingPass: vkCreateDescriptorSetLayout failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 3. Descriptor pool: maxFrames sets, 9 combined image samplers each
		// -----------------------------------------------------------------
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 9 * m_maxFrames;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = m_maxFrames;

			VkResult res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "LightingPass: vkCreateDescriptorPool failed: {}", static_cast<int>(res));
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
				LOG_ERROR(Render, "LightingPass: vkAllocateDescriptorSets failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 5. Samplers (nearest clamp for GBuffer; nearest clamp for depth)
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
			si.compareOp     = VK_COMPARE_OP_ALWAYS;
			si.maxLod        = 0.0f;

			VkResult res = vkCreateSampler(device, &si, nullptr, &m_sampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "LightingPass: vkCreateSampler (color) failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}

			// Depth sampler: identical settings, no compare enable (plain sampling).
			res = vkCreateSampler(device, &si, nullptr, &m_depthSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "LightingPass: vkCreateSampler (depth) failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		// -----------------------------------------------------------------
		// 6. Pipeline layout: descriptor set 0 + push constants (132 bytes, fragment)
		// -----------------------------------------------------------------
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pushRange.offset     = 0;
			pushRange.size       = static_cast<uint32_t>(sizeof(LightParams)); // 132 bytes (M05.4 useIBL)

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount         = 1;
			layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushRange;

			VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
			LOG_INFO(Render, "[LIGHT] vkCreatePipelineLayout r={}", (int)res);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "LightingPass: vkCreatePipelineLayout failed: {}", static_cast<int>(res));
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
				LOG_ERROR(Render, "LightingPass: shader module creation failed");
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
			rs.cullMode    = VK_CULL_MODE_NONE; // no culling for fullscreen triangle
			rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// No depth test/write for the fullscreen lighting pass.
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
			LOG_DEBUG(Render, "[LIGHT] vkCreateGraphicsPipelines r={}", (int)res);
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "LightingPass: vkCreateGraphicsPipelines failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
		}

		LOG_INFO(Render, "LightingPass: initialized (maxFrames={})", m_maxFrames);
		return true;
	}

	// -------------------------------------------------------------------------
	// LightingPass::Record
	// -------------------------------------------------------------------------

	void LightingPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent,
		ResourceId idGBufA, ResourceId idGBufB, ResourceId idGBufC, ResourceId idDepth,
		ResourceId idSceneColorHDR, ResourceId idSsaoBlur, ResourceId idDecalOverlay,
		VkImageView irradianceView, VkSampler irradianceSampler,
		VkImageView prefilterView, VkSampler prefilterSampler,
		VkImageView brdfLutView, VkSampler brdfLutSampler,
		const LightParams& params, uint32_t frameIndex)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0)
			return;
		LOG_INFO(Render, "[LightingPass] Record enter");

		// Retrieve image views from the frame graph registry.
		VkImageView viewA    = registry.getImageView(idGBufA);
		VkImageView viewB    = registry.getImageView(idGBufB);
		VkImageView viewC    = registry.getImageView(idGBufC);
		VkImageView viewD    = registry.getImageView(idDepth);
		VkImageView viewHDR   = registry.getImageView(idSceneColorHDR);
		VkImageView viewSsao  = registry.getImageView(idSsaoBlur);
		VkImageView viewDecal = registry.getImageView(idDecalOverlay);

		if (viewA == VK_NULL_HANDLE || viewB == VK_NULL_HANDLE
			|| viewC == VK_NULL_HANDLE || viewD == VK_NULL_HANDLE
			|| viewHDR == VK_NULL_HANDLE || viewSsao == VK_NULL_HANDLE || viewDecal == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "LightingPass::Record: missing image views, skipping");
			return;
		}
		LOG_INFO(Render, "[LightingPass] Views resolved");

		// IBL: when irradiance absent bind prefilter for slot 4 so descriptor valid; params.useIBL is 0.
		VkImageView irrView = (irradianceView != VK_NULL_HANDLE) ? irradianceView : prefilterView;
		VkSampler   irrSamp = (irradianceSampler != VK_NULL_HANDLE) ? irradianceSampler : prefilterSampler;
		if (prefilterView == VK_NULL_HANDLE) { prefilterView = viewA; prefilterSampler = m_sampler; }
		if (brdfLutView == VK_NULL_HANDLE) { brdfLutView = viewA; brdfLutSampler = m_sampler; }
		if (irrView == VK_NULL_HANDLE) { irrView = viewA; irrSamp = m_sampler; }

		// ------------------------------------------------------------------
		// Update descriptor set for this frame with GBuffer + IBL views.
		// ------------------------------------------------------------------
		const uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		std::array<VkDescriptorImageInfo, 9> imageInfos{};
		imageInfos[0] = { m_sampler,         viewA,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_sampler,         viewB,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[2] = { m_sampler,         viewC,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[3] = { m_depthSampler,    viewD,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[4] = { irrSamp,           irrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[5] = { prefilterSampler,  prefilterView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[6] = { brdfLutSampler,    brdfLutView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[7] = { m_sampler,         viewSsao, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[8] = { m_sampler,         viewDecal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		std::array<VkWriteDescriptorSet, 9> writes{};
		for (size_t i = 0; i < writes.size(); ++i)
		{
			writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet          = ds;
			writes[i].dstBinding      = i;
			writes[i].dstArrayElement = 0;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo      = &imageInfos[i];
		}
		LOG_INFO(Render, "[LightingPass] Updating descriptors");
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		LOG_INFO(Render, "[LightingPass] Descriptors updated");

		// ------------------------------------------------------------------
		// Create a temporary framebuffer for this frame.
		// ------------------------------------------------------------------
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments    = &viewHDR;
		fbInfo.width           = extent.width;
		fbInfo.height          = extent.height;
		fbInfo.layers          = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;
		LOG_INFO(Render, "[LightingPass] Creating framebuffer");
		VkResult res = vkCreateFramebuffer(device, &fbInfo, nullptr, &fb);
		if (res != VK_SUCCESS)
		{
			LOG_ERROR(Render, "LightingPass::Record: vkCreateFramebuffer failed: {}", static_cast<int>(res));
			return;
		}
		LOG_INFO(Render, "[LightingPass] Framebuffer created");

		// ------------------------------------------------------------------
		// Begin render pass.
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

		LOG_INFO(Render, "[LightingPass] Begin render pass");
		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		LOG_INFO(Render, "[LightingPass] Render pass begun");

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

		// Push constants: invVP + camera/light params (128 bytes, fragment stage).
		LOG_INFO(Render, "[LightingPass] Push constants");
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			0, static_cast<uint32_t>(sizeof(LightParams)), &params);

		// Draw fullscreen triangle (3 vertices, no vertex buffer).
		LOG_INFO(Render, "[LightingPass] Draw");
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);
		LOG_INFO(Render, "[LightingPass] Render pass ended");

		// Destroy the temporary framebuffer immediately.
		vkDestroyFramebuffer(device, fb, nullptr);
		LOG_INFO(Render, "[LightingPass] Record done");
	}

	// -------------------------------------------------------------------------
	// LightingPass::Destroy
	// -------------------------------------------------------------------------

	void LightingPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
		{
			LOG_INFO(Render, "[LightingPass] Destroyed");
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
		if (m_depthSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_depthSampler, nullptr);
			m_depthSampler = VK_NULL_HANDLE;
		}
		if (m_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_sampler, nullptr);
			m_sampler = VK_NULL_HANDLE;
		}
		// Descriptor sets are implicitly freed when the pool is destroyed.
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
		LOG_INFO(Render, "[LightingPass] Destroyed");
	}

} // namespace engine::render
