#include "engine/render/GeometryPass.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <cstring>
#include <functional>

namespace engine::render
{
	namespace
	{
		/// M07.3: prevViewProj (64) + viewProj (64) for motion vectors.
		constexpr uint32_t kPushConstantSize = 128u;

		void UploadIdentityInstanceMatrix(VkDevice device, VkDeviceMemory memory, const float* instanceMatrix)
		{
			if (device == VK_NULL_HANDLE || memory == VK_NULL_HANDLE || !instanceMatrix)
				return;

			void* mapped = nullptr;
			if (vkMapMemory(device, memory, 0, 64u, 0, &mapped) == VK_SUCCESS && mapped)
			{
				std::memcpy(mapped, instanceMatrix, 64u);
				vkUnmapMemory(device, memory);
			}
			else
			{
				LOG_WARN(Render, "[GeometryPass] Instance matrix upload failed");
			}
		}
	}

	bool GeometryPass::FramebufferKey::operator==(const FramebufferKey& o) const
	{
		if (renderPass != o.renderPass || width != o.width || height != o.height)
			return false;
		for (int i = 0; i < 5; ++i)
			if (views[i] != o.views[i]) return false;
		return true;
	}

	size_t GeometryPass::FramebufferKeyHash::operator()(const FramebufferKey& k) const
	{
		size_t h = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.renderPass));
		h ^= std::hash<uint32_t>{}(k.width) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
		h ^= std::hash<uint32_t>{}(k.height) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
		for (int i = 0; i < 5; ++i)
			h ^= std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.views[i])) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
		return h;
	}

	bool GeometryPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
	    VkFormat formatA, VkFormat formatB, VkFormat formatC, VkFormat formatVelocity, VkFormat depthFormat,
	    const uint32_t* vertSpirv, size_t vertWordCount,
	    const uint32_t* fragSpirv, size_t fragWordCount,
	    VkDescriptorSetLayout materialLayout)
	{
		if (!device || !vertSpirv || vertWordCount == 0 || !fragSpirv || fragWordCount == 0)
			return false;

		// -------------------------------------------------------------------------
		// Render pass: 3 colour attachments (GBuf A/B/C) + 1 depth attachment.
		// -------------------------------------------------------------------------
		VkAttachmentDescription attachments[4] = {};
		attachments[0].format        = formatA;
		attachments[0].samples       = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		attachments[1].format        = formatB;
		attachments[1].samples       = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		attachments[2].format        = formatC;
		attachments[2].samples       = VK_SAMPLE_COUNT_1_BIT;
		attachments[2].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[2].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// M07.3: velocity (R16G16F).
		attachments[3].format        = formatVelocity;
		attachments[3].samples       = VK_SAMPLE_COUNT_1_BIT;
		attachments[3].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[3].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[3].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		attachments[4].format        = depthFormat;
		attachments[4].samples       = VK_SAMPLE_COUNT_1_BIT;
		attachments[4].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[4].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[4].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[4].finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRefs[4] = {
			{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
			{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
			{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
			{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
		};
		VkAttachmentReference depthRef = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount    = 4;
		subpass.pColorAttachments       = colorRefs;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dep = {};
		dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass    = 0;
		dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dep.srcAccessMask = 0;
		dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo = {};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 5;
		rpInfo.pAttachments    = attachments;
		rpInfo.subpassCount    = 1;
		rpInfo.pSubpasses      = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dep;

		VkResult result = vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "GeometryPass: vkCreateRenderPass failed: {}", static_cast<int>(result));
			return false;
		}

		// -------------------------------------------------------------------------
		// Pipeline layout:
		//   - set 0 (optional): material descriptor set layout (BaseColor/Normal/ORM)
		//   - push constant: prevViewProj + viewProj (vertex stage, 128 bytes, M07.3)
		// -------------------------------------------------------------------------
		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset     = 0;
		pushRange.size       = kPushConstantSize;

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges    = &pushRange;

		// Include the material descriptor set layout only if one was provided.
		if (materialLayout != VK_NULL_HANDLE)
		{
			layoutInfo.setLayoutCount = 1;
			layoutInfo.pSetLayouts    = &materialLayout;
			m_hasMaterialLayout = true;
		}

		result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "GeometryPass: vkCreatePipelineLayout failed: {}", static_cast<int>(result));
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
			return false;
		}

		// -------------------------------------------------------------------------
		// Shader modules.
		// -------------------------------------------------------------------------
		VkShaderModule vertModule = VK_NULL_HANDLE;
		VkShaderModule fragModule = VK_NULL_HANDLE;
		{
			VkShaderModuleCreateInfo modInfo = {};
			modInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			modInfo.pCode    = vertSpirv;
			modInfo.codeSize = vertWordCount * sizeof(uint32_t);
			result = vkCreateShaderModule(device, &modInfo, nullptr, &vertModule);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "GeometryPass: vertex shader module failed: {}", static_cast<int>(result));
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				vkDestroyRenderPass(device, m_renderPass, nullptr);
				m_pipelineLayout = VK_NULL_HANDLE;
				m_renderPass     = VK_NULL_HANDLE;
				return false;
			}

			modInfo.pCode    = fragSpirv;
			modInfo.codeSize = fragWordCount * sizeof(uint32_t);
			result = vkCreateShaderModule(device, &modInfo, nullptr, &fragModule);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "GeometryPass: fragment shader module failed: {}", static_cast<int>(result));
				vkDestroyShaderModule(device, vertModule, nullptr);
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				vkDestroyRenderPass(device, m_renderPass, nullptr);
				m_pipelineLayout = VK_NULL_HANDLE;
				m_renderPass     = VK_NULL_HANDLE;
				return false;
			}
		}

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName  = "main";
		stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName  = "main";

		// -------------------------------------------------------------------------
		// Vertex input: binding 0 = vertex (pos,norm,uv); binding 1 = instance mat4 (M09.3).
		// -------------------------------------------------------------------------
		VkVertexInputAttributeDescription attrs[7] = {};
		attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
		attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 12;
		attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT;    attrs[2].offset = 24;
		attrs[3].location = 3; attrs[3].binding = 1; attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[3].offset = 0;
		attrs[4].location = 4; attrs[4].binding = 1; attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[4].offset = 16;
		attrs[5].location = 5; attrs[5].binding = 1; attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[5].offset = 32;
		attrs[6].location = 6; attrs[6].binding = 1; attrs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[6].offset = 48;

		VkVertexInputBindingDescription bindings[2] = {};
		bindings[0].binding   = 0;
		bindings[0].stride    = 32;
		bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		bindings[1].binding   = 1;
		bindings[1].stride    = 64;
		bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount   = 2;
		vi.pVertexBindingDescriptions      = bindings;
		vi.vertexAttributeDescriptionCount = 7;
		vi.pVertexAttributeDescriptions    = attrs;

		VkPipelineInputAssemblyStateCreateInfo ia = {};
		ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp = {};
		vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount  = 1;

		VkPipelineRasterizationStateCreateInfo rs = {};
		rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode    = VK_CULL_MODE_BACK_BIT;
		rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth   = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds = {};
		ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable  = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendAttachmentState blendAtt[4] = {};
		for (auto& att : blendAtt)
			att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 4;
		cb.pAttachments    = blendAtt;

		VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn = {};
		dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates    = dynStates;

		VkGraphicsPipelineCreateInfo gpInfo = {};
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

		result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline);
		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "GeometryPass: vkCreateGraphicsPipelines failed: {}", static_cast<int>(result));
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
			m_renderPass     = VK_NULL_HANDLE;
			return false;
		}

		// M09.3: identity instance buffer (one mat4) for single-instance draw (binding 1).
		{
			constexpr VkDeviceSize kIdentityBufferSize = 64u;
			VkBufferCreateInfo bufInfo = {};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size  = kIdentityBufferSize;
			bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			result = vkCreateBuffer(device, &bufInfo, nullptr, &m_identityInstanceBuffer);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "GeometryPass: identity instance buffer create failed: {}", static_cast<int>(result));
				vkDestroyPipeline(device, m_pipeline, nullptr);
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				vkDestroyRenderPass(device, m_renderPass, nullptr);
				m_pipeline = VK_NULL_HANDLE;
				m_pipelineLayout = VK_NULL_HANDLE;
				m_renderPass = VK_NULL_HANDLE;
				return false;
			}
			VkMemoryRequirements memReq;
			vkGetBufferMemoryRequirements(device, m_identityInstanceBuffer, &memReq);
			VkPhysicalDeviceMemoryProperties memProps;
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
			uint32_t memTypeIndex = UINT32_MAX;
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((memReq.memoryTypeBits & (1u << i)) != 0
					&& (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0
					&& (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
				{
					memTypeIndex = i;
					break;
				}
			}
			if (memTypeIndex == UINT32_MAX)
			{
				vkDestroyBuffer(device, m_identityInstanceBuffer, nullptr);
				m_identityInstanceBuffer = VK_NULL_HANDLE;
				vkDestroyPipeline(device, m_pipeline, nullptr);
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				vkDestroyRenderPass(device, m_renderPass, nullptr);
				m_pipeline = VK_NULL_HANDLE;
				m_pipelineLayout = VK_NULL_HANDLE;
				m_renderPass = VK_NULL_HANDLE;
				return false;
			}
			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize  = memReq.size;
			allocInfo.memoryTypeIndex = memTypeIndex;
			result = vkAllocateMemory(device, &allocInfo, nullptr, &m_identityInstanceMemory);
			if (result != VK_SUCCESS)
			{
				vkDestroyBuffer(device, m_identityInstanceBuffer, nullptr);
				m_identityInstanceBuffer = VK_NULL_HANDLE;
				vkDestroyPipeline(device, m_pipeline, nullptr);
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				vkDestroyRenderPass(device, m_renderPass, nullptr);
				m_pipeline = VK_NULL_HANDLE;
				m_pipelineLayout = VK_NULL_HANDLE;
				m_renderPass = VK_NULL_HANDLE;
				return false;
			}
			vkBindBufferMemory(device, m_identityInstanceBuffer, m_identityInstanceMemory, 0);
			float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
			void* ptr = nullptr;
			vkMapMemory(device, m_identityInstanceMemory, 0, kIdentityBufferSize, 0, &ptr);
			if (ptr) { std::memcpy(ptr, identity, 64); vkUnmapMemory(device, m_identityInstanceMemory); }
		}
		LOG_INFO(Render, "[Boot] GeometryPass init OK");
		return true;
	}

	// -------------------------------------------------------------------------
	// GeometryPass::Record
	// -------------------------------------------------------------------------

	void GeometryPass::Record(VkDevice device, VkCommandBuffer cmd,
	    Registry& registry, VkExtent2D extent,
	    ResourceId idA, ResourceId idB, ResourceId idC, ResourceId idVelocity, ResourceId idDepth,
	    const float* prevViewProjMat4, const float* viewProjMat4, const MeshAsset* mesh,
	    uint32_t lodLevel,
	    VkDescriptorSet materialDescriptorSet,
	    const float* instanceMatrix)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0)
			return;

		VkImageView viewA      = registry.getImageView(idA);
		VkImageView viewB      = registry.getImageView(idB);
		VkImageView viewC      = registry.getImageView(idC);
		VkImageView viewVel    = registry.getImageView(idVelocity);
		VkImageView viewDepth  = registry.getImageView(idDepth);
		if (!viewA || !viewB || !viewC || !viewVel || !viewDepth)
			return;

		FramebufferKey key{};
		key.renderPass = m_renderPass;
		key.views[0]   = viewA;
		key.views[1]   = viewB;
		key.views[2]   = viewC;
		key.views[3]   = viewVel;
		key.views[4]   = viewDepth;
		key.width      = extent.width;
		key.height     = extent.height;

		auto it = m_fbCache.find(key);
		if (it == m_fbCache.end())
		{
			VkImageView views[5] = { viewA, viewB, viewC, viewVel, viewDepth };
			VkFramebufferCreateInfo fbInfo = {};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = m_renderPass;
			fbInfo.attachmentCount = 5;
			fbInfo.pAttachments    = views;
			fbInfo.width           = extent.width;
			fbInfo.height         = extent.height;
			fbInfo.layers         = 1;
			VkFramebuffer created = VK_NULL_HANDLE;
			if (vkCreateFramebuffer(device, &fbInfo, nullptr, &created) != VK_SUCCESS)
				return;
			it = m_fbCache.emplace(key, created).first;
		}
		VkFramebuffer fb = it->second;

		VkClearValue clearValues[5] = {};
		clearValues[0].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[2].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[3].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } }; // velocity
		clearValues[4].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rpBegin = {};
		rpBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass      = m_renderPass;
		rpBegin.framebuffer     = fb;
		rpBegin.renderArea      = { { 0, 0 }, extent };
		rpBegin.clearValueCount = 5;
		rpBegin.pClearValues    = clearValues;

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

		VkViewport viewport = {};
		viewport.width    = static_cast<float>(extent.width);
		viewport.height   = static_cast<float>(extent.height);
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// M07.3: push prevViewProj (64 bytes) then viewProj (64 bytes).
		if (prevViewProjMat4 && viewProjMat4)
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
			                   kPushConstantSize, prevViewProjMat4);
		if (viewProjMat4)
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 64,
			                   64u, viewProjMat4);

		// Bind material descriptor set (set = 0) if the pass was initialised with one.
		if (m_hasMaterialLayout && materialDescriptorSet != VK_NULL_HANDLE)
		{
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			    m_pipelineLayout, 0, 1, &materialDescriptorSet, 0, nullptr);
		}

		if (mesh
			&& mesh->vertexBuffer != VK_NULL_HANDLE
			&& mesh->indexBuffer  != VK_NULL_HANDLE
			&& m_identityInstanceBuffer != VK_NULL_HANDLE)
		{
			if (instanceMatrix && m_identityInstanceMemory != VK_NULL_HANDLE)
				UploadIdentityInstanceMatrix(device, m_identityInstanceMemory, instanceMatrix);

			const uint32_t indexCount = mesh->GetLodIndexCount(lodLevel);
			if (indexCount > 0)
			{
				const uint32_t indexOffset = mesh->GetLodIndexOffset(lodLevel);
				VkBuffer vb[2] = { mesh->vertexBuffer, m_identityInstanceBuffer };
				VkDeviceSize vbOffsets[2] = { 0, 0 };
				vkCmdBindVertexBuffers(cmd, 0, 2, vb, vbOffsets);
				vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(cmd, indexCount, 1, indexOffset, 0, 0);
			}
		}

		vkCmdEndRenderPass(cmd);
	}

	void GeometryPass::RecordIndirect(VkDevice device, VkCommandBuffer cmd,
	    Registry& registry, VkExtent2D extent,
	    ResourceId idA, ResourceId idB, ResourceId idC, ResourceId idVelocity, ResourceId idDepth,
	    const float* prevViewProjMat4, const float* viewProjMat4, const MeshAsset* mesh,
	    VkBuffer indirectBuffer, uint32_t indirectDrawCount,
	    VkDescriptorSet materialDescriptorSet,
	    const float* instanceMatrix)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0)
			return;

		VkImageView viewA      = registry.getImageView(idA);
		VkImageView viewB      = registry.getImageView(idB);
		VkImageView viewC      = registry.getImageView(idC);
		VkImageView viewVel    = registry.getImageView(idVelocity);
		VkImageView viewDepth  = registry.getImageView(idDepth);
		if (!viewA || !viewB || !viewC || !viewVel || !viewDepth)
			return;

		FramebufferKey key{};
		key.renderPass = m_renderPass;
		key.views[0]   = viewA;
		key.views[1]   = viewB;
		key.views[2]   = viewC;
		key.views[3]   = viewVel;
		key.views[4]   = viewDepth;
		key.width      = extent.width;
		key.height     = extent.height;

		auto it = m_fbCache.find(key);
		if (it == m_fbCache.end())
		{
			VkImageView views[5] = { viewA, viewB, viewC, viewVel, viewDepth };
			VkFramebufferCreateInfo fbInfo = {};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = m_renderPass;
			fbInfo.attachmentCount = 5;
			fbInfo.pAttachments    = views;
			fbInfo.width           = extent.width;
			fbInfo.height          = extent.height;
			fbInfo.layers          = 1;
			VkFramebuffer created  = VK_NULL_HANDLE;
			if (vkCreateFramebuffer(device, &fbInfo, nullptr, &created) != VK_SUCCESS)
				return;
			it = m_fbCache.emplace(key, created).first;
		}
		VkFramebuffer fb = it->second;

		VkClearValue clearValues[5] = {};
		clearValues[0].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[2].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[3].color        = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[4].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rpBegin = {};
		rpBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass      = m_renderPass;
		rpBegin.framebuffer     = fb;
		rpBegin.renderArea      = { { 0, 0 }, extent };
		rpBegin.clearValueCount = 5;
		rpBegin.pClearValues    = clearValues;

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

		VkViewport viewport = {};
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		if (prevViewProjMat4 && viewProjMat4)
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, kPushConstantSize, prevViewProjMat4);
		if (viewProjMat4)
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 64, 64u, viewProjMat4);

		if (m_hasMaterialLayout && materialDescriptorSet != VK_NULL_HANDLE)
		{
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_pipelineLayout, 0, 1, &materialDescriptorSet, 0, nullptr);
		}

		if (mesh
			&& mesh->vertexBuffer != VK_NULL_HANDLE
			&& mesh->indexBuffer != VK_NULL_HANDLE
			&& m_identityInstanceBuffer != VK_NULL_HANDLE
			&& indirectBuffer != VK_NULL_HANDLE
			&& indirectDrawCount > 0)
		{
			if (instanceMatrix && m_identityInstanceMemory != VK_NULL_HANDLE)
				UploadIdentityInstanceMatrix(device, m_identityInstanceMemory, instanceMatrix);

			VkBuffer vb[2] = { mesh->vertexBuffer, m_identityInstanceBuffer };
			VkDeviceSize vbOffsets[2] = { 0, 0 };
			vkCmdBindVertexBuffers(cmd, 0, 2, vb, vbOffsets);
			vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexedIndirect(cmd, indirectBuffer, 0,
				indirectDrawCount, sizeof(VkDrawIndexedIndirectCommand));
		}

		vkCmdEndRenderPass(cmd);
	}

	// -------------------------------------------------------------------------
	// GeometryPass::Destroy
	// -------------------------------------------------------------------------

	void GeometryPass::InvalidateFramebufferCache(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
		for (auto& p : m_fbCache)
		{
			if (p.second != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device, p.second, nullptr);
		}
		m_fbCache.clear();
	}

	void GeometryPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
		if (m_identityInstanceBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_identityInstanceBuffer, nullptr);
			m_identityInstanceBuffer = VK_NULL_HANDLE;
		}
		if (m_identityInstanceMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_identityInstanceMemory, nullptr);
			m_identityInstanceMemory = VK_NULL_HANDLE;
		}
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
		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}
		m_hasMaterialLayout = false;
		LOG_INFO(Render, "[GeometryPass] Destroyed");
	}

	// -------------------------------------------------------------------------
	// GeometryPass::RecordInstanced (M09.3)
	// -------------------------------------------------------------------------

	void GeometryPass::RecordInstanced(VkDevice device, VkCommandBuffer cmd,
	    Registry& registry, VkExtent2D extent,
	    ResourceId idA, ResourceId idB, ResourceId idC, ResourceId idVelocity, ResourceId idDepth,
	    const float* prevViewProjMat4, const float* viewProjMat4,
	    const InstanceBatch* batches, uint32_t batchCount,
	    VkBuffer instanceBuffer)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0 || !batches || batchCount == 0
			|| instanceBuffer == VK_NULL_HANDLE)
			return;

		VkImageView viewA = registry.getImageView(idA);
		VkImageView viewB = registry.getImageView(idB);
		VkImageView viewC = registry.getImageView(idC);
		VkImageView viewVel = registry.getImageView(idVelocity);
		VkImageView viewDepth = registry.getImageView(idDepth);
		if (!viewA || !viewB || !viewC || !viewVel || !viewDepth)
			return;

		VkImageView views[5] = { viewA, viewB, viewC, viewVel, viewDepth };
		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = m_renderPass;
		fbInfo.attachmentCount = 5;
		fbInfo.pAttachments = views;
		fbInfo.width = extent.width;
		fbInfo.height = extent.height;
		fbInfo.layers = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fbInfo, nullptr, &fb) != VK_SUCCESS)
			return;

		VkClearValue clearValues[5] = {};
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[3].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[4].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rpBegin = {};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = m_renderPass;
		rpBegin.framebuffer = fb;
		rpBegin.renderArea = { { 0, 0 }, extent };
		rpBegin.clearValueCount = 5;
		rpBegin.pClearValues = clearValues;

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

		VkViewport viewport = {};
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		if (prevViewProjMat4 && viewProjMat4)
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, kPushConstantSize, prevViewProjMat4);
		if (viewProjMat4)
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 64, 64u, viewProjMat4);

		for (uint32_t i = 0; i < batchCount; ++i)
		{
			const InstanceBatch& batch = batches[i];
			if (!batch.mesh || batch.instanceCount == 0
				|| batch.mesh->vertexBuffer == VK_NULL_HANDLE
				|| batch.mesh->indexBuffer == VK_NULL_HANDLE)
				continue;
			const uint32_t indexCount = batch.mesh->GetLodIndexCount(batch.lodLevel);
			if (indexCount == 0) continue;

			if (m_hasMaterialLayout && batch.materialDescriptorSet != VK_NULL_HANDLE)
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &batch.materialDescriptorSet, 0, nullptr);

			VkBuffer vb[2] = { batch.mesh->vertexBuffer, instanceBuffer };
			VkDeviceSize vbOffsets[2] = { 0, static_cast<VkDeviceSize>(batch.instanceBufferOffset) };
			vkCmdBindVertexBuffers(cmd, 0, 2, vb, vbOffsets);
			vkCmdBindIndexBuffer(cmd, batch.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			const uint32_t indexOffset = batch.mesh->GetLodIndexOffset(batch.lodLevel);
			vkCmdDrawIndexed(cmd, indexCount, batch.instanceCount, indexOffset, 0, 0);
		}

		vkCmdEndRenderPass(cmd);
		vkDestroyFramebuffer(device, fb, nullptr);
	}

} // namespace engine::render
