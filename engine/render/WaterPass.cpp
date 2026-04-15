#include "engine/render/WaterPass.h"
#include "engine/core/Log.h"

#include <cstring>
#include <vector>

namespace engine::render
{
	namespace
	{
		// ── Water grid constants ──────────────────────────────────────────────
		static constexpr uint32_t kGridDivisions = 32u; ///< Quads per side.
		static constexpr uint32_t kGridVerts     = kGridDivisions + 1u; ///< Vertices per side (33).

		// ── Vulkan helpers ────────────────────────────────────────────────────

		/// Find a memory type index matching \p desired flags; returns UINT32_MAX on failure.
		static uint32_t FindMemType(VkPhysicalDevice physDev,
		                            uint32_t         typeBits,
		                            VkMemoryPropertyFlags desired)
		{
			VkPhysicalDeviceMemoryProperties props{};
			vkGetPhysicalDeviceMemoryProperties(physDev, &props);
			for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
			{
				if ((typeBits & (1u << i)) &&
				    (props.memoryTypes[i].propertyFlags & desired) == desired)
					return i;
			}
			return UINT32_MAX;
		}

		/// Allocate HOST_VISIBLE | HOST_COHERENT buffer and copy \p srcData into it.
		static bool CreateHostBuffer(VkDevice              device,
		                             VkPhysicalDevice      physDev,
		                             VkDeviceSize          size,
		                             VkBufferUsageFlags    usage,
		                             const void*           srcData,
		                             VkBuffer&             outBuf,
		                             VkDeviceMemory&       outMem)
		{
			VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			bi.size        = size;
			bi.usage       = usage;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (vkCreateBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateBuffer failed (size={} usage=0x{:x})", size, usage);
				return false;
			}

			VkMemoryRequirements req{};
			vkGetBufferMemoryRequirements(device, outBuf, &req);

			const uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (memType == UINT32_MAX)
			{
				LOG_ERROR(Render, "[WaterPass] No HOST_VISIBLE memory type available");
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = memType;

			if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkAllocateMemory failed");
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}
			vkBindBufferMemory(device, outBuf, outMem, 0);

			void* mapped = nullptr;
			vkMapMemory(device, outMem, 0, size, 0, &mapped);
			std::memcpy(mapped, srcData, static_cast<size_t>(size));
			vkUnmapMemory(device, outMem);
			return true;
		}

		/// Create a DEVICE_LOCAL VkImage + VkImageView for a color render target.
		static bool CreateRenderTarget(VkDevice              device,
		                               VkPhysicalDevice      physDev,
		                               VkFormat              format,
		                               uint32_t              width,
		                               uint32_t              height,
		                               VkImage&              outImage,
		                               VkDeviceMemory&       outMemory,
		                               VkImageView&          outView)
		{
			VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			ici.imageType   = VK_IMAGE_TYPE_2D;
			ici.format      = format;
			ici.extent      = {width, height, 1};
			ici.mipLevels   = 1;
			ici.arrayLayers = 1;
			ici.samples     = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
			ici.usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			if (vkCreateImage(device, &ici, nullptr, &outImage) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateImage failed ({}x{})", width, height);
				return false;
			}

			VkMemoryRequirements req{};
			vkGetImageMemoryRequirements(device, outImage, &req);

			const uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memType == UINT32_MAX)
			{
				LOG_ERROR(Render, "[WaterPass] No DEVICE_LOCAL memory type for render target");
				vkDestroyImage(device, outImage, nullptr);
				outImage = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			mai.allocationSize  = req.size;
			mai.memoryTypeIndex = memType;
			if (vkAllocateMemory(device, &mai, nullptr, &outMemory) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkAllocateMemory failed for render target");
				vkDestroyImage(device, outImage, nullptr);
				outImage = VK_NULL_HANDLE;
				return false;
			}
			vkBindImageMemory(device, outImage, outMemory, 0);

			VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			vci.image    = outImage;
			vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vci.format   = format;
			vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			if (vkCreateImageView(device, &vci, nullptr, &outView) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateImageView failed");
				vkFreeMemory(device, outMemory, nullptr);
				vkDestroyImage(device, outImage, nullptr);
				outImage  = VK_NULL_HANDLE;
				outMemory = VK_NULL_HANDLE;
				return false;
			}
			return true;
		}

		/// Build the 32x32 water plane geometry (vertices in [0,1]^2 XZ, expanded in shader).
		static void BuildWaterPlane(std::vector<float>& outVerts, std::vector<uint16_t>& outIndices)
		{
			outVerts.clear();
			outVerts.reserve(kGridVerts * kGridVerts * 2);
			for (uint32_t z = 0; z < kGridVerts; ++z)
			{
				for (uint32_t x = 0; x < kGridVerts; ++x)
				{
					outVerts.push_back(static_cast<float>(x) / static_cast<float>(kGridDivisions));
					outVerts.push_back(static_cast<float>(z) / static_cast<float>(kGridDivisions));
				}
			}

			outIndices.clear();
			outIndices.reserve(kGridDivisions * kGridDivisions * 6);
			for (uint32_t z = 0; z < kGridDivisions; ++z)
			{
				for (uint32_t x = 0; x < kGridDivisions; ++x)
				{
					const uint16_t tl = static_cast<uint16_t>(z * kGridVerts + x);
					const uint16_t tr = static_cast<uint16_t>(tl + 1u);
					const uint16_t bl = static_cast<uint16_t>((z + 1u) * kGridVerts + x);
					const uint16_t br = static_cast<uint16_t>(bl + 1u);
					outIndices.push_back(tl);
					outIndices.push_back(bl);
					outIndices.push_back(tr);
					outIndices.push_back(tr);
					outIndices.push_back(bl);
					outIndices.push_back(br);
				}
			}
		}

		/// 4x4 column-major matrix multiply: out = a * b.
		static void Mat4Mul(const float* a, const float* b, float* out)
		{
			for (int col = 0; col < 4; ++col)
			{
				for (int row = 0; row < 4; ++row)
				{
					float sum = 0.0f;
					for (int k = 0; k < 4; ++k)
						sum += a[k * 4 + row] * b[col * 4 + k];
					out[col * 4 + row] = sum;
				}
			}
		}
	} // anonymous namespace

	// ── WaterPass public API ─────────────────────────────────────────────────

	bool WaterPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t rtWidth, uint32_t rtHeight,
		uint32_t maxFrames, VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: invalid device or physical device");
			return false;
		}
		if (!vertSpirv || vertWordCount == 0 || !fragSpirv || fragWordCount == 0)
		{
			LOG_WARN(Render, "[WaterPass] Init skipped: water shaders not provided");
			return false;
		}
		if (rtWidth == 0 || rtHeight == 0)
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: invalid RT dimensions ({}x{})", rtWidth, rtHeight);
			return false;
		}

		m_hdrFormat  = sceneColorHDRFormat;
		m_maxFrames  = maxFrames;

		// ── 1. Water plane mesh ──────────────────────────────────────────────
		std::vector<float>    verts;
		std::vector<uint16_t> indices;
		BuildWaterPlane(verts, indices);
		m_indexCount = static_cast<uint32_t>(indices.size());

		const VkDeviceSize vertBytes  = verts.size()   * sizeof(float);
		const VkDeviceSize indexBytes = indices.size() * sizeof(uint16_t);

		if (!CreateHostBuffer(device, physicalDevice,
			vertBytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			verts.data(),
			m_vertexBuffer, m_vertexMemory))
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: vertex buffer creation failed");
			return false;
		}
		if (!CreateHostBuffer(device, physicalDevice,
			indexBytes,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			indices.data(),
			m_indexBuffer, m_indexMemory))
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: index buffer creation failed");
			Destroy(device);
			return false;
		}
		LOG_INFO(Render, "[WaterPass] Water plane mesh created ({}x{} grid, {} verts, {} indices)",
			kGridDivisions, kGridDivisions,
			verts.size() / 2u, m_indexCount);

		// ── 2. Reflection render target (half-res HDR) ───────────────────────
		if (!CreateRenderTarget(device, physicalDevice, sceneColorHDRFormat,
			rtWidth, rtHeight,
			m_reflectionImage, m_reflectionMemory, m_reflectionView))
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: reflection RT creation failed");
			Destroy(device);
			return false;
		}
		LOG_INFO(Render, "[WaterPass] Reflection RT created ({}x{} {})",
			rtWidth, rtHeight, static_cast<int>(sceneColorHDRFormat));

		// ── 3. Refraction render target (half-res HDR) ───────────────────────
		if (!CreateRenderTarget(device, physicalDevice, sceneColorHDRFormat,
			rtWidth, rtHeight,
			m_refractionImage, m_refractionMemory, m_refractionView))
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: refraction RT creation failed");
			Destroy(device);
			return false;
		}
		LOG_INFO(Render, "[WaterPass] Refraction RT created ({}x{})", rtWidth, rtHeight);

		// ── 4a. Color sampler (linear, clamp-to-edge) for reflection/refraction ──
		{
			VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			si.magFilter    = VK_FILTER_LINEAR;
			si.minFilter    = VK_FILTER_LINEAR;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.maxLod       = VK_LOD_CLAMP_NONE;
			if (vkCreateSampler(device, &si, nullptr, &m_sampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: color sampler creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 4b. Depth sampler (nearest, clamp) for foam depth comparison (M37.2) ──
		{
			VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			si.magFilter    = VK_FILTER_NEAREST;
			si.minFilter    = VK_FILTER_NEAREST;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.maxLod       = 0.0f;
			if (vkCreateSampler(device, &si, nullptr, &m_depthSampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: depth sampler creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 5. Render pass (composite water onto existing SceneColorHDR) ────
		{
			VkAttachmentDescription color{};
			color.format         = sceneColorHDRFormat;
			color.samples        = VK_SAMPLE_COUNT_1_BIT;
			color.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD; ///< Preserve existing scene.
			color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			color.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
			rci.pAttachments    = &color;
			rci.subpassCount    = 1;
			rci.pSubpasses      = &subpass;
			rci.dependencyCount = 1;
			rci.pDependencies   = &dep;

			if (vkCreateRenderPass(device, &rci, nullptr, &m_renderPass) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: render pass creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 6. Descriptor set layout ─────────────────────────────────────────
		{
			VkDescriptorSetLayoutBinding bindings[3]{};
			// Binding 0: reflection texture
			bindings[0].binding         = 0;
			bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[0].descriptorCount = 1;
			bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			// Binding 1: refraction texture
			bindings[1].binding         = 1;
			bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[1].descriptorCount = 1;
			bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			// Binding 2: scene depth texture for foam (M37.2)
			bindings[2].binding         = 2;
			bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[2].descriptorCount = 1;
			bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			dlci.bindingCount = 3;
			dlci.pBindings    = bindings;
			if (vkCreateDescriptorSetLayout(device, &dlci, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: descriptor set layout creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 7. Descriptor pool + sets ────────────────────────────────────────
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 3u * maxFrames; ///< reflection + refraction + depth (M37.2)

			VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			dpci.maxSets       = maxFrames;
			dpci.poolSizeCount = 1;
			dpci.pPoolSizes    = &poolSize;
			if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: descriptor pool creation failed");
				Destroy(device);
				return false;
			}

			m_descriptorSets.resize(maxFrames);
			for (uint32_t i = 0; i < maxFrames; ++i)
			{
				VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
				dsai.descriptorPool     = m_descriptorPool;
				dsai.descriptorSetCount = 1;
				dsai.pSetLayouts        = &m_descriptorSetLayout;
				if (vkAllocateDescriptorSets(device, &dsai, &m_descriptorSets[i]) != VK_SUCCESS)
				{
					LOG_ERROR(Render, "[WaterPass] Init FAILED: descriptor set allocation failed");
					Destroy(device);
					return false;
				}
			}

			// Pre-write the reflection and refraction image views once.
			for (uint32_t i = 0; i < maxFrames; ++i)
			{
				VkDescriptorImageInfo reflectInfo{};
				reflectInfo.sampler     = m_sampler;
				reflectInfo.imageView   = m_reflectionView;
				reflectInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				VkDescriptorImageInfo refractInfo{};
				refractInfo.sampler     = m_sampler;
				refractInfo.imageView   = m_refractionView;
				refractInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				VkWriteDescriptorSet writes[2]{};
				writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[0].dstSet          = m_descriptorSets[i];
				writes[0].dstBinding      = 0;
				writes[0].descriptorCount = 1;
				writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[0].pImageInfo      = &reflectInfo;

				writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[1].dstSet          = m_descriptorSets[i];
				writes[1].dstBinding      = 1;
				writes[1].descriptorCount = 1;
				writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[1].pImageInfo      = &refractInfo;

				vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
			}
		}

		// ── 8. Pipeline layout (push constants: 120 bytes, all stages) ───────
		{
			VkPushConstantRange pcr{};
			pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pcr.offset     = 0;
			pcr.size       = static_cast<uint32_t>(sizeof(WaterParams));

			VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			plci.setLayoutCount         = 1;
			plci.pSetLayouts            = &m_descriptorSetLayout;
			plci.pushConstantRangeCount = 1;
			plci.pPushConstantRanges    = &pcr;

			if (vkCreatePipelineLayout(device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: pipeline layout creation failed");
				Destroy(device);
				return false;
			}
		}

		// ── 9. Graphics pipeline ─────────────────────────────────────────────
		{
			VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			VkShaderModule vertMod = VK_NULL_HANDLE;
			VkShaderModule fragMod = VK_NULL_HANDLE;

			smci.codeSize = vertWordCount * sizeof(uint32_t);
			smci.pCode    = vertSpirv;
			if (vkCreateShaderModule(device, &smci, nullptr, &vertMod) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: vert shader module creation failed");
				Destroy(device);
				return false;
			}
			smci.codeSize = fragWordCount * sizeof(uint32_t);
			smci.pCode    = fragSpirv;
			if (vkCreateShaderModule(device, &smci, nullptr, &fragMod) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: frag shader module creation failed");
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

			// Vertex input: location 0, binding 0, vec2 (XZ in [0,1])
			VkVertexInputBindingDescription vbd{};
			vbd.binding   = 0;
			vbd.stride    = 2 * sizeof(float);
			vbd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription vad{};
			vad.location = 0;
			vad.binding  = 0;
			vad.format   = VK_FORMAT_R32G32_SFLOAT;
			vad.offset   = 0;

			VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
			vi.vertexBindingDescriptionCount   = 1;
			vi.pVertexBindingDescriptions      = &vbd;
			vi.vertexAttributeDescriptionCount = 1;
			vi.pVertexAttributeDescriptions    = &vad;

			VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
			vps.viewportCount = 1;
			vps.scissorCount  = 1;

			VkPipelineRasterizationStateCreateInfo ras{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
			ras.polygonMode = VK_POLYGON_MODE_FILL;
			ras.cullMode    = VK_CULL_MODE_NONE; ///< No culling — visible from above and below.
			ras.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			ras.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Alpha blending: water is semi-transparent.
			VkPipelineColorBlendAttachmentState blendState{};
			blendState.blendEnable         = VK_TRUE;
			blendState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendState.colorBlendOp        = VK_BLEND_OP_ADD;
			blendState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendState.alphaBlendOp        = VK_BLEND_OP_ADD;
			blendState.colorWriteMask      =
				VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
			cbs.attachmentCount = 1;
			cbs.pAttachments    = &blendState;

			// No depth testing for the water composite pass.
			VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
			ds.depthTestEnable  = VK_FALSE;
			ds.depthWriteEnable = VK_FALSE;

			constexpr VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
			dynState.dynamicStateCount = 2;
			dynState.pDynamicStates    = dynStates;

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
			gpci.pDynamicState       = &dynState;
			gpci.layout              = m_pipelineLayout;
			gpci.renderPass          = m_renderPass;
			gpci.subpass             = 0;

			const VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpci, nullptr, &m_pipeline);

			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);

			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] Init FAILED: graphics pipeline creation failed");
				Destroy(device);
				return false;
			}
		}

		LOG_INFO(Render, "[WaterPass] Init OK (rtSize={}x{}, gridDivisions={}, maxFrames={})",
			rtWidth, rtHeight, kGridDivisions, maxFrames);
		return true;
	}

	void WaterPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent,
		ResourceId idSceneColorHDR,
		ResourceId idDepth,
		const WaterParams& params,
		uint32_t frameIndex)
	{
		if (!IsValid())
			return;

		const VkImageView sceneHDRView = registry.getImageView(idSceneColorHDR);
		if (sceneHDRView == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[WaterPass] Record skipped: SceneColorHDR view not bound");
			return;
		}

		// M37.2 — Update binding 2 with the scene depth view for foam.
		// vkUpdateDescriptorSets is a host operation, safe to call before the render pass.
		const uint32_t fi = frameIndex % m_maxFrames;
		const VkImageView depthView = registry.getImageView(idDepth);
		if (depthView != VK_NULL_HANDLE)
		{
			VkDescriptorImageInfo depthInfo{};
			depthInfo.sampler     = m_depthSampler;
			depthInfo.imageView   = depthView;
			depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet depthWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
			depthWrite.dstSet          = m_descriptorSets[fi];
			depthWrite.dstBinding      = 2;
			depthWrite.descriptorCount = 1;
			depthWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			depthWrite.pImageInfo      = &depthInfo;
			vkUpdateDescriptorSets(device, 1, &depthWrite, 0, nullptr);
		}

		// Create a temporary framebuffer for this frame's extent.
		VkFramebuffer fb = VK_NULL_HANDLE;
		{
			VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
			fci.renderPass      = m_renderPass;
			fci.attachmentCount = 1;
			fci.pAttachments    = &sceneHDRView;
			fci.width           = extent.width;
			fci.height          = extent.height;
			fci.layers          = 1;
			if (vkCreateFramebuffer(device, &fci, nullptr, &fb) != VK_SUCCESS)
			{
				LOG_WARN(Render, "[WaterPass] Record: framebuffer creation failed — skipping");
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
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(WaterParams), &params);

		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

		vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);

		vkCmdEndRenderPass(cmd);

		vkDestroyFramebuffer(device, fb, nullptr);

		LOG_TRACE(Render, "[WaterPass] Record (frame={})", frameIndex);
	}

	void WaterPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (m_pipeline            != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
		if (m_pipelineLayout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
		if (m_descriptorPool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
		if (m_descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
		if (m_renderPass          != VK_NULL_HANDLE) vkDestroyRenderPass(device, m_renderPass, nullptr);
		if (m_sampler             != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);
		if (m_depthSampler        != VK_NULL_HANDLE) vkDestroySampler(device, m_depthSampler, nullptr);

		if (m_reflectionView   != VK_NULL_HANDLE) vkDestroyImageView(device, m_reflectionView,   nullptr);
		if (m_reflectionImage  != VK_NULL_HANDLE) vkDestroyImage(device, m_reflectionImage,  nullptr);
		if (m_reflectionMemory != VK_NULL_HANDLE) vkFreeMemory(device, m_reflectionMemory, nullptr);

		if (m_refractionView   != VK_NULL_HANDLE) vkDestroyImageView(device, m_refractionView,   nullptr);
		if (m_refractionImage  != VK_NULL_HANDLE) vkDestroyImage(device, m_refractionImage,  nullptr);
		if (m_refractionMemory != VK_NULL_HANDLE) vkFreeMemory(device, m_refractionMemory, nullptr);

		if (m_vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, m_vertexBuffer, nullptr);
		if (m_vertexMemory != VK_NULL_HANDLE) vkFreeMemory(device, m_vertexMemory, nullptr);
		if (m_indexBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(device, m_indexBuffer, nullptr);
		if (m_indexMemory  != VK_NULL_HANDLE) vkFreeMemory(device, m_indexMemory, nullptr);

		m_pipeline = VK_NULL_HANDLE;
		m_pipelineLayout = VK_NULL_HANDLE;
		m_descriptorPool = VK_NULL_HANDLE;
		m_descriptorSetLayout = VK_NULL_HANDLE;
		m_renderPass = VK_NULL_HANDLE;
		m_sampler = VK_NULL_HANDLE;
		m_depthSampler = VK_NULL_HANDLE;
		m_reflectionView = m_refractionView = VK_NULL_HANDLE;
		m_reflectionImage = m_refractionImage = VK_NULL_HANDLE;
		m_reflectionMemory = m_refractionMemory = VK_NULL_HANDLE;
		m_vertexBuffer = m_indexBuffer = VK_NULL_HANDLE;
		m_vertexMemory = m_indexMemory = VK_NULL_HANDLE;
		m_indexCount = 0;
		m_descriptorSets.clear();

		LOG_INFO(Render, "[WaterPass] Destroyed");
	}

	void WaterPass::BuildReflectionViewProj(const float* viewProj, float waterY, float* outReflectVP)
	{
		// Reflection matrix about the horizontal plane y = waterY.
		// Maps (x, y, z, 1) → (x, 2*waterY - y, z, 1).
		// Column-major: columns are basis vectors.
		const float reflectMat[16] = {
			1.0f,    0.0f, 0.0f, 0.0f,  // column 0
			0.0f,   -1.0f, 0.0f, 0.0f,  // column 1 (flip Y)
			0.0f,    0.0f, 1.0f, 0.0f,  // column 2
			0.0f, 2.0f * waterY, 0.0f, 1.0f  // column 3 (translation)
		};
		// Reflected VP = VP * reflectMat  (apply reflection in world space before projecting).
		Mat4Mul(viewProj, reflectMat, outReflectVP);
	}

} // namespace engine::render
