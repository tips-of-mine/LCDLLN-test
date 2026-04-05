#include "engine/render/AuthGlyphPass.h"

#include "engine/core/Log.h"
#include "engine/render/vk/VkUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace engine::render
{
	namespace
	{
		struct PushConstants
		{
			float viewportSize[2];
		};
		static_assert(sizeof(PushConstants) == 8, "PushConstants must be 8 bytes");

		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t wordCount)
		{
			if (device == VK_NULL_HANDLE || code == nullptr || wordCount == 0)
			{
				return VK_NULL_HANDLE;
			}
			VkShaderModuleCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode = code;
			VkShaderModule module = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
			{
				return VK_NULL_HANDLE;
			}
			return module;
		}

		std::array<uint8_t, 7> GlyphRows(char raw)
		{
			switch (static_cast<unsigned char>(raw))
			{
			case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
			case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
			case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
			case 'D': return { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
			case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
			case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
			case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
			case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
			case 'I': return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
			case 'J': return { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
			case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
			case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
			case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
			case 'N': return { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 };
			case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
			case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
			case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
			case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
			case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
			case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
			case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
			case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
			case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
			case 'X': return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
			case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
			case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
			case 'a': return { 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F };
			case 'b': return { 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E };
			case 'c': return { 0x00, 0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E };
			case 'd': return { 0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F };
			case 'e': return { 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E };
			case 'f': return { 0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x08 };
			case 'g': return { 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E };
			case 'h': return { 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11 };
			case 'i': return { 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E };
			case 'j': return { 0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C };
			case 'k': return { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12 };
			case 'l': return { 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
			case 'm': return { 0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15 };
			case 'n': return { 0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11 };
			case 'o': return { 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E };
			case 'p': return { 0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10 };
			case 'q': return { 0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01 };
			case 'r': return { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 };
			case 's': return { 0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E };
			case 't': return { 0x08, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x06 };
			case 'u': return { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D };
			case 'v': return { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04 };
			case 'w': return { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A };
			case 'x': return { 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11 };
			case 'y': return { 0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E };
			case 'z': return { 0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F };
			case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
			case '1': return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
			case '2': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
			case '3': return { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E };
			case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
			case '5': return { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
			case '6': return { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
			case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
			case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
			case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
			case ':': return { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
			case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
			case ',': return { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x08 };
			case '\'': return { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 };
			case '"': return { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 };
			case ';': return { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x08 };
			case '-': return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
			case '_': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F };
			case '/': return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
			case '(': return { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
			case ')': return { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
			case '[': return { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E };
			case ']': return { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E };
			case '%': return { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
			case '+': return { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
			case '>': return { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 };
			case '<': return { 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01 };
			case '?': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
			case '!': return { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
			case '=': return { 0x00, 0x1F, 0x00, 0x00, 0x1F, 0x00, 0x00 };
			case ' ': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			default:  return { 0x1F, 0x11, 0x02, 0x04, 0x08, 0x11, 0x1F };
			}
		}

		std::string SimplifyUtf8(std::string_view text)
		{
			std::string out;
			out.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i)
			{
				const unsigned char c = static_cast<unsigned char>(text[i]);
				if (c < 0x80u)
				{
					out.push_back(static_cast<char>(c));
				}
				else if (c == 0xC3u && (i + 1u) < text.size())
				{
					const unsigned char d = static_cast<unsigned char>(text[++i]);
					switch (d)
					{
					case 0x80: case 0x82: case 0x84: out.push_back('A'); break;
					case 0x87: out.push_back('C'); break;
					case 0x88: case 0x89: case 0x8A: case 0x8B: out.push_back('E'); break;
					case 0x8E: case 0x8F: out.push_back('I'); break;
					case 0x94: case 0x96: out.push_back('O'); break;
					case 0x99: case 0x9B: case 0x9C: out.push_back('U'); break;
					case 0xA0: case 0xA2: case 0xA4: out.push_back('a'); break;
					case 0xA7: out.push_back('c'); break;
					case 0xA8: case 0xA9: case 0xAA: case 0xAB: out.push_back('e'); break;
					case 0xAE: case 0xAF: out.push_back('i'); break;
					case 0xB4: case 0xB6: out.push_back('o'); break;
					case 0xB9: case 0xBB: case 0xBC: out.push_back('u'); break;
					case 0xBF: out.push_back('y'); break;
					default: out.push_back('?'); break;
					}
				}
				else if (c == 0xC5u && (i + 1u) < text.size())
				{
					const unsigned char d = static_cast<unsigned char>(text[++i]);
					switch (d)
					{
					case 0x92: out += "OE"; break;
					case 0x93: out += "oe"; break;
					default: out.push_back('?'); break;
					}
				}
				else
				{
					out.push_back('?');
				}
			}
			return out;
		}

		int32_t GlyphWidthPx(char c, int32_t scale)
		{
			const int32_t s = std::max(1, scale);
			switch (static_cast<unsigned char>(c))
			{
			case 'i': case 'l': case '.': case ',': case ':': case ';': case '!': case '\'':
				return 2 * s;
			case '"': case 'f': case 'j': case 'r': case 't': case '(': case ')': case '[': case ']':
				return 3 * s;
			case ' ':
				return 3 * s;
			case 'm': case 'w': case 'M': case 'W':
				return 6 * s;
			default:
				return 5 * s;
			}
		}

		int32_t GlyphAdvancePx(char c, int32_t scale)
		{
			return GlyphWidthPx(c, scale) + std::max(1, scale);
		}

		int32_t TextPixelWidth(std::string_view text, int32_t scale)
		{
			std::string normalized = SimplifyUtf8(text);
			int32_t width = 0;
			for (char ch : normalized)
			{
				if (ch == '\r' || ch == '\n')
				{
					break;
				}
				width += GlyphAdvancePx(ch, scale);
			}
			return width;
		}

		void PackRows(const std::array<uint8_t, 7>& rows, uint32_t& bits0, uint32_t& bits1)
		{
			bits0 = static_cast<uint32_t>(rows[0])
				| (static_cast<uint32_t>(rows[1]) << 8)
				| (static_cast<uint32_t>(rows[2]) << 16)
				| (static_cast<uint32_t>(rows[3]) << 24);
			bits1 = static_cast<uint32_t>(rows[4])
				| (static_cast<uint32_t>(rows[5]) << 8)
				| (static_cast<uint32_t>(rows[6]) << 16);
		}
	}

	bool AuthGlyphPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		VkFormat colorFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxGlyphs,
		VkPipelineCache pipelineCache,
		const uint32_t* fragTtfSpirv,
		size_t fragTtfWordCount)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || vertSpirv == nullptr || fragSpirv == nullptr
			|| vertWordCount == 0 || fragWordCount == 0 || maxGlyphs == 0)
		{
			LOG_ERROR(Render, "AuthGlyphPass::Init invalid parameters");
			return false;
		}

		m_maxGlyphs = maxGlyphs;
		m_maxVertices = maxGlyphs * 6u;

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			Destroy(device);
			return false;
		}

		VkShaderModule vertModule = CreateShaderModule(device, vertSpirv, vertWordCount);
		VkShaderModule fragModule = CreateShaderModule(device, fragSpirv, fragWordCount);
		if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
		{
			if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
			if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
			Destroy(device);
			return false;
		}

		VkVertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(GlyphVertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		std::array<VkVertexInputAttributeDescription, 5> attrs{};
		attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GlyphVertex, pos) };
		attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GlyphVertex, uv) };
		attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GlyphVertex, color) };
		attrs[3] = { 3, 0, VK_FORMAT_R32_UINT, offsetof(GlyphVertex, bits0) };
		attrs[4] = { 4, 0, VK_FORMAT_R32_UINT, offsetof(GlyphVertex, bits1) };

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = 1;
		vi.pVertexBindingDescriptions = &binding;
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
		vi.pVertexAttributeDescriptions = attrs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blend{};
		blend.blendEnable = VK_TRUE;
		blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.colorBlendOp = VK_BLEND_OP_ADD;
		blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.alphaBlendOp = VK_BLEND_OP_ADD;
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &blend;

		VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn{};
		dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates = dynStates;

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName = "main";

		VkPipelineRenderingCreateInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &colorFormat;

		VkGraphicsPipelineCreateInfo gp{};
		gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gp.pNext = &renderingInfo;
		gp.stageCount = 2;
		gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vp;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pColorBlendState = &cb;
		gp.pDynamicState = &dyn;
		gp.layout = m_pipelineLayout;
		if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &gp, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			vkDestroyShaderModule(device, vertModule, nullptr);
			vkDestroyShaderModule(device, fragModule, nullptr);
			Destroy(device);
			return false;
		}

		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = static_cast<VkDeviceSize>(sizeof(GlyphVertex)) * static_cast<VkDeviceSize>(m_maxVertices);
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_vertexBuffer) != VK_SUCCESS)
		{
			Destroy(device);
			return false;
		}

		VkMemoryRequirements memReq{};
		vkGetBufferMemoryRequirements(device, m_vertexBuffer, &memReq);
		const uint32_t memType = engine::render::vk::FindMemoryType(physicalDevice, memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memType == UINT32_MAX)
		{
			Destroy(device);
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memType;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_vertexMemory) != VK_SUCCESS)
		{
			Destroy(device);
			return false;
		}
		if (vkBindBufferMemory(device, m_vertexBuffer, m_vertexMemory, 0) != VK_SUCCESS)
		{
			Destroy(device);
			return false;
		}
		if (vkMapMemory(device, m_vertexMemory, 0, bufferInfo.size, 0, &m_mappedVertices) != VK_SUCCESS)
		{
			Destroy(device);
			return false;
		}

		if (fragTtfSpirv != nullptr && fragTtfWordCount > 0u)
		{
			if (!CreateFontPipeline(device, physicalDevice, colorFormat, vertSpirv, vertWordCount, fragTtfSpirv, fragTtfWordCount, pipelineCache))
			{
				LOG_WARN(Render, "[AuthGlyphPass] Pipeline TTF non créé — texte bitmap uniquement");
			}
		}

		return true;
	}

	void AuthGlyphPass::DestroyFontGpu(VkDevice device)
	{
		m_fontGpuReady = false;
		m_uiFont = FontAtlasTtf{};
		if (m_fontImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_fontImageView, nullptr);
			m_fontImageView = VK_NULL_HANDLE;
		}
		if (m_fontImage != VK_NULL_HANDLE)
		{
			vkDestroyImage(device, m_fontImage, nullptr);
			m_fontImage = VK_NULL_HANDLE;
		}
		if (m_fontImageMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_fontImageMemory, nullptr);
			m_fontImageMemory = VK_NULL_HANDLE;
		}
		if (m_fontSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_fontSampler, nullptr);
			m_fontSampler = VK_NULL_HANDLE;
		}
		if (m_fontPipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, m_fontPipeline, nullptr);
			m_fontPipeline = VK_NULL_HANDLE;
		}
		if (m_fontPipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device, m_fontPipelineLayout, nullptr);
			m_fontPipelineLayout = VK_NULL_HANDLE;
		}
		if (m_fontDescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_fontDescriptorPool, nullptr);
			m_fontDescriptorPool = VK_NULL_HANDLE;
		}
		m_fontDescriptorSet = VK_NULL_HANDLE;
		if (m_fontDescriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_fontDescriptorSetLayout, nullptr);
			m_fontDescriptorSetLayout = VK_NULL_HANDLE;
		}
	}

	bool AuthGlyphPass::CreateFontPipeline(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragTtfSpirv, size_t fragTtfWordCount,
		VkPipelineCache pipelineCache)
	{
		DestroyFontGpu(device);

		VkDescriptorSetLayoutBinding bind{};
		bind.binding = 0;
		bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bind.descriptorCount = 1;
		bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo dsl{};
		dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		dsl.bindingCount = 1;
		dsl.pBindings = &bind;
		if (vkCreateDescriptorSetLayout(device, &dsl, nullptr, &m_fontDescriptorSetLayout) != VK_SUCCESS)
		{
			return false;
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 1;
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_fontDescriptorPool) != VK_SUCCESS)
		{
			vkDestroyDescriptorSetLayout(device, m_fontDescriptorSetLayout, nullptr);
			m_fontDescriptorSetLayout = VK_NULL_HANDLE;
			return false;
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_fontDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_fontDescriptorSetLayout;
		if (vkAllocateDescriptorSets(device, &allocInfo, &m_fontDescriptorSet) != VK_SUCCESS)
		{
			DestroyFontGpu(device);
			return false;
		}

		VkSamplerCreateInfo samp{};
		samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samp.magFilter = VK_FILTER_LINEAR;
		samp.minFilter = VK_FILTER_LINEAR;
		samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		if (vkCreateSampler(device, &samp, nullptr, &m_fontSampler) != VK_SUCCESS)
		{
			DestroyFontGpu(device);
			return false;
		}

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &m_fontDescriptorSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_fontPipelineLayout) != VK_SUCCESS)
		{
			DestroyFontGpu(device);
			return false;
		}

		VkShaderModule vertModule = CreateShaderModule(device, vertSpirv, vertWordCount);
		VkShaderModule fragModule = CreateShaderModule(device, fragTtfSpirv, fragTtfWordCount);
		if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
		{
			if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
			if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
			DestroyFontGpu(device);
			return false;
		}

		VkVertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(GlyphVertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		std::array<VkVertexInputAttributeDescription, 5> attrs{};
		attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GlyphVertex, pos) };
		attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GlyphVertex, uv) };
		attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GlyphVertex, color) };
		attrs[3] = { 3, 0, VK_FORMAT_R32_UINT, offsetof(GlyphVertex, bits0) };
		attrs[4] = { 4, 0, VK_FORMAT_R32_UINT, offsetof(GlyphVertex, bits1) };

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = 1;
		vi.pVertexBindingDescriptions = &binding;
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
		vi.pVertexAttributeDescriptions = attrs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blend{};
		blend.blendEnable = VK_TRUE;
		blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.colorBlendOp = VK_BLEND_OP_ADD;
		blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.alphaBlendOp = VK_BLEND_OP_ADD;
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &blend;

		VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn{};
		dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates = dynStates;

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName = "main";

		VkPipelineRenderingCreateInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &colorFormat;

		VkGraphicsPipelineCreateInfo gp{};
		gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gp.pNext = &renderingInfo;
		gp.stageCount = 2;
		gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vp;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pColorBlendState = &cb;
		gp.pDynamicState = &dyn;
		gp.layout = m_fontPipelineLayout;
		const VkResult gpResult = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gp, nullptr, &m_fontPipeline);
		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
		if (gpResult != VK_SUCCESS || m_fontPipeline == VK_NULL_HANDLE)
		{
			DestroyFontGpu(device);
			return false;
		}

		return true;
	}

	namespace
	{
		void ReleaseUiFontImageOnly(VkDevice device, VkImage& image, VkDeviceMemory& mem, VkImageView& view)
		{
			if (view != VK_NULL_HANDLE)
			{
				vkDestroyImageView(device, view, nullptr);
				view = VK_NULL_HANDLE;
			}
			if (image != VK_NULL_HANDLE)
			{
				vkDestroyImage(device, image, nullptr);
				image = VK_NULL_HANDLE;
			}
			if (mem != VK_NULL_HANDLE)
			{
				vkFreeMemory(device, mem, nullptr);
				mem = VK_NULL_HANDLE;
			}
		}

		bool Utf8Next(std::string_view s, size_t& i, int& cp)
		{
			if (i >= s.size())
			{
				return false;
			}
			const unsigned char c0 = static_cast<unsigned char>(s[i]);
			if (c0 < 0x80u)
			{
				cp = c0;
				++i;
				return true;
			}
			if ((c0 & 0xE0u) == 0xC0u && i + 1u < s.size())
			{
				const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
				cp = (static_cast<int>(c0 & 0x1Fu) << 6) | static_cast<int>(c1 & 0x3Fu);
				i += 2;
				return true;
			}
			if ((c0 & 0xF0u) == 0xE0u && i + 2u < s.size())
			{
				const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
				const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
				cp = (static_cast<int>(c0 & 0x0Fu) << 12) | (static_cast<int>(c1 & 0x3Fu) << 6) | static_cast<int>(c2 & 0x3Fu);
				i += 3;
				return true;
			}
			if ((c0 & 0xF8u) == 0xF0u && i + 3u < s.size())
			{
				const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
				const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
				const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
				cp = (static_cast<int>(c0 & 0x07u) << 18) | (static_cast<int>(c1 & 0x3Fu) << 12) | (static_cast<int>(c2 & 0x3Fu) << 6)
					| static_cast<int>(c3 & 0x3Fu);
				i += 4;
				return true;
			}
			cp = '?';
			++i;
			return true;
		}
	}

	bool AuthGlyphPass::UploadUiFontFromTtf(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, uint32_t queueFamilyIndex,
		const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight)
	{
		if (m_fontPipeline == VK_NULL_HANDLE || m_fontDescriptorSet == VK_NULL_HANDLE || ttfBytes == nullptr || ttfSize == 0u)
		{
			return false;
		}

		m_fontGpuReady = false;
		ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);

		FontAtlasTtf baked{};
		if (!baked.BuildFromMemory(ttfBytes, ttfSize, pixelHeight))
		{
			LOG_WARN(Render, "[AuthGlyphPass] Échec construction atlas TTF");
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		m_uiFont = std::move(baked);

		const uint32_t w = static_cast<uint32_t>(m_uiFont.AtlasWidth());
		const uint32_t h = static_cast<uint32_t>(m_uiFont.AtlasHeight());
		const std::vector<uint8_t>& pixels = m_uiFont.AtlasR8();
		if (w == 0u || h == 0u || pixels.size() < static_cast<size_t>(w) * static_cast<size_t>(h))
		{
			m_uiFont = FontAtlasTtf{};
			return false;
		}

		VkImageCreateInfo imgInfo{};
		imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.imageType = VK_IMAGE_TYPE_2D;
		imgInfo.format = VK_FORMAT_R8_UNORM;
		imgInfo.extent = { w, h, 1 };
		imgInfo.mipLevels = 1;
		imgInfo.arrayLayers = 1;
		imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateImage(device, &imgInfo, nullptr, &m_fontImage) != VK_SUCCESS || m_fontImage == VK_NULL_HANDLE)
		{
			m_uiFont = FontAtlasTtf{};
			return false;
		}

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(device, m_fontImage, &memReq);
		const uint32_t memTypeIdx = engine::render::vk::FindMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (memTypeIdx == UINT32_MAX)
		{
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		VkMemoryAllocateInfo allocImg{};
		allocImg.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocImg.allocationSize = memReq.size;
		allocImg.memoryTypeIndex = memTypeIdx;
		if (vkAllocateMemory(device, &allocImg, nullptr, &m_fontImageMemory) != VK_SUCCESS)
		{
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		if (vkBindImageMemory(device, m_fontImage, m_fontImageMemory, 0) != VK_SUCCESS)
		{
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}

		const VkDeviceSize stagingSize = static_cast<VkDeviceSize>(pixels.size());
		VkBufferCreateInfo bufInfo{};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = stagingSize;
		bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		VkBuffer stagingBuf = VK_NULL_HANDLE;
		if (vkCreateBuffer(device, &bufInfo, nullptr, &stagingBuf) != VK_SUCCESS)
		{
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		VkMemoryRequirements bufReq{};
		vkGetBufferMemoryRequirements(device, stagingBuf, &bufReq);
		const uint32_t stagingMemIdx = engine::render::vk::FindMemoryType(physicalDevice, bufReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		VkMemoryAllocateInfo stagingAlloc{};
		stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		stagingAlloc.allocationSize = bufReq.size;
		stagingAlloc.memoryTypeIndex = stagingMemIdx;
		if (stagingMemIdx == UINT32_MAX || vkAllocateMemory(device, &stagingAlloc, nullptr, &stagingMem) != VK_SUCCESS)
		{
			vkDestroyBuffer(device, stagingBuf, nullptr);
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		if (vkBindBufferMemory(device, stagingBuf, stagingMem, 0) != VK_SUCCESS)
		{
			vkFreeMemory(device, stagingMem, nullptr);
			vkDestroyBuffer(device, stagingBuf, nullptr);
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		void* mapPtr = nullptr;
		if (vkMapMemory(device, stagingMem, 0, bufReq.size, 0, &mapPtr) != VK_SUCCESS)
		{
			vkFreeMemory(device, stagingMem, nullptr);
			vkDestroyBuffer(device, stagingBuf, nullptr);
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		std::memcpy(mapPtr, pixels.data(), pixels.size());
		vkUnmapMemory(device, stagingMem);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_fontImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R8_UNORM;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_fontImageView) != VK_SUCCESS)
		{
			vkFreeMemory(device, stagingMem, nullptr);
			vkDestroyBuffer(device, stagingBuf, nullptr);
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = queueFamilyIndex;
		VkCommandPool pool = VK_NULL_HANDLE;
		if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
		{
			vkFreeMemory(device, stagingMem, nullptr);
			vkDestroyBuffer(device, stagingBuf, nullptr);
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}
		VkCommandBufferAllocateInfo cbAlloc{};
		cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbAlloc.commandPool = pool;
		cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cbAlloc.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(device, &cbAlloc, &cmd) != VK_SUCCESS)
		{
			vkDestroyCommandPool(device, pool, nullptr);
			vkFreeMemory(device, stagingMem, nullptr);
			vkDestroyBuffer(device, stagingBuf, nullptr);
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &beginInfo);

		VkImageMemoryBarrier toDst{};
		toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toDst.srcAccessMask = 0;
		toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toDst.image = m_fontImage;
		toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { w, h, 1 };
		vkCmdCopyBufferToImage(cmd, stagingBuf, m_fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VkImageMemoryBarrier toShader{};
		toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader.image = m_fontImage;
		toShader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);

		vkEndCommandBuffer(cmd);

		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		const VkResult sub = vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
		vkQueueWaitIdle(graphicsQueue);
		vkDestroyCommandPool(device, pool, nullptr);
		vkFreeMemory(device, stagingMem, nullptr);
		vkDestroyBuffer(device, stagingBuf, nullptr);

		if (sub != VK_SUCCESS)
		{
			ReleaseUiFontImageOnly(device, m_fontImage, m_fontImageMemory, m_fontImageView);
			m_uiFont = FontAtlasTtf{};
			return false;
		}

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = m_fontSampler;
		imageInfo.imageView = m_fontImageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_fontDescriptorSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

		m_fontGpuReady = true;
		LOG_INFO(Render, "[AuthGlyphPass] Police UI TTF upload OK ({}x{})", w, h);
		return true;
	}

	void AuthGlyphPass::AppendTextTtf(std::vector<GlyphVertex>& vertices,
		std::string_view text,
		int32_t originX, int32_t originY,
		int32_t maxWidthPx,
		int32_t scale,
		const float color[4]) const
	{
		if (text.empty() || !m_uiFont.IsValid())
		{
			return;
		}
		const float mul = std::max(0.35f, static_cast<float>(scale) / 4.f);
		const int32_t maxX = originX + std::max(32, maxWidthPx);
		int32_t lineIndex = 0;
		float penX = 0.f;

		auto emitQuad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
		{
			if (vertices.size() + 6u > m_maxVertices)
			{
				return;
			}
			const uint32_t z = 0u;
			const float sx0 = static_cast<float>(originX) + x0 * mul;
			const float sy0 = static_cast<float>(originY) + y0 * mul;
			const float sx1 = static_cast<float>(originX) + x1 * mul;
			const float sy1 = static_cast<float>(originY) + y1 * mul;
			const GlyphVertex quad[6] = {
				{ { sx0, sy0 }, { u0, v0 }, { color[0], color[1], color[2], color[3] }, z, z },
				{ { sx1, sy0 }, { u1, v0 }, { color[0], color[1], color[2], color[3] }, z, z },
				{ { sx1, sy1 }, { u1, v1 }, { color[0], color[1], color[2], color[3] }, z, z },
				{ { sx0, sy0 }, { u0, v0 }, { color[0], color[1], color[2], color[3] }, z, z },
				{ { sx1, sy1 }, { u1, v1 }, { color[0], color[1], color[2], color[3] }, z, z },
				{ { sx0, sy1 }, { u0, v1 }, { color[0], color[1], color[2], color[3] }, z, z }
			};
			vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
		};

		size_t i = 0;
		while (i < text.size())
		{
			int cp = 0;
			if (!Utf8Next(text, i, cp))
			{
				break;
			}
			if (cp == '\r')
			{
				continue;
			}
			if (cp == '\n')
			{
				penX = 0.f;
				++lineIndex;
				continue;
			}
			const float advPx = m_uiFont.GlyphXAdvance(cp) * mul;
			if (static_cast<int32_t>(std::floor(static_cast<float>(originX) + penX + advPx)) > maxX && penX > 0.f)
			{
				penX = 0.f;
				++lineIndex;
				if (cp == ' ')
				{
					continue;
				}
			}
			const float lineBaseY = static_cast<float>(m_uiFont.LineTopToBaselinePx() + lineIndex * m_uiFont.LineHeightPx());
			float pen = penX;
			float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f, s0 = 0.f, t0 = 0.f, s1 = 0.f, t1 = 0.f, xadv = 0.f;
			if (!m_uiFont.GetPackedQuad(cp, &pen, lineBaseY, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1, &xadv))
			{
				continue;
			}
			penX = pen;
			emitQuad(x0, y0, x1, y1, s0, t0, s1, t1);
			if (vertices.size() + 6u > m_maxVertices || vertices.size() / 6u >= m_maxGlyphs)
			{
				break;
			}
		}
	}

	void AuthGlyphPass::AppendText(std::vector<GlyphVertex>& vertices,
		std::string_view text,
		int32_t originX, int32_t originY,
		int32_t maxWidthPx,
		int32_t scale,
		const float color[4]) const
	{
		if (text.empty())
		{
			return;
		}
		if (m_fontGpuReady && m_uiFont.IsValid())
		{
			AppendTextTtf(vertices, text, originX, originY, maxWidthPx, scale, color);
			return;
		}
		std::string normalized = SimplifyUtf8(text);
		const int32_t linePitch = (7 * std::max(1, scale)) + (2 * std::max(1, scale));
		const int32_t maxX = originX + std::max(32, maxWidthPx);
		int32_t cursorX = originX;
		int32_t cursorY = originY;
		auto appendGlyph = [&vertices, scale, this, color](int32_t x, int32_t y, char ch)
		{
			if (vertices.size() + 6u > m_maxVertices)
			{
				return;
			}
			uint32_t bits0 = 0;
			uint32_t bits1 = 0;
			PackRows(GlyphRows(ch), bits0, bits1);
			const float left = static_cast<float>(x);
			const float top = static_cast<float>(y);
			const float right = static_cast<float>(x + 5 * scale);
			const float bottom = static_cast<float>(y + 7 * scale);
			const GlyphVertex quad[6] = {
				{{ left, top },    { 0.0f, 0.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ right, top },   { 1.0f, 0.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ right, bottom },{ 1.0f, 1.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ left, top },    { 0.0f, 0.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ right, bottom },{ 1.0f, 1.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ left, bottom }, { 0.0f, 1.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1}
			};
			vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
		};
		for (char ch : normalized)
		{
			if (ch == '\r')
			{
				continue;
			}
			if (ch == '\n')
			{
				cursorX = originX;
				cursorY += linePitch;
				continue;
			}
			const int32_t advance = GlyphAdvancePx(ch, scale);
			if (cursorX + advance > maxX)
			{
				cursorX = originX;
				cursorY += linePitch;
				if (ch == ' ')
				{
					continue;
				}
			}
			appendGlyph(cursorX, cursorY, ch);
			cursorX += advance;
			if (vertices.size() + 6u > m_maxVertices || vertices.size() / 6u >= m_maxGlyphs)
			{
				break;
			}
		}
	}

	int32_t AuthGlyphPass::MeasureTextWidthPx(std::string_view text, int32_t scale) const
	{
		if (m_fontGpuReady && m_uiFont.IsValid())
		{
			const float mul = std::max(0.35f, static_cast<float>(scale) / 4.f);
			return static_cast<int32_t>(std::lround(static_cast<float>(m_uiFont.MeasureWidthPx(text)) * mul));
		}
		return TextPixelWidth(text, scale);
	}

	void AuthGlyphPass::Record(VkDevice device, VkCommandBuffer cmd, VkExtent2D extent,
		std::string_view text,
		int32_t originX, int32_t originY,
		int32_t maxWidthPx)
	{
		if (!IsValid() || cmd == VK_NULL_HANDLE || m_mappedVertices == nullptr || extent.width == 0 || extent.height == 0)
		{
			return;
		}
		std::vector<GlyphVertex> vertices;
		vertices.reserve(std::min<uint32_t>(m_maxVertices, static_cast<uint32_t>(text.size() * 6u)));
		const float color[4] = { 0.93f, 0.95f, 0.98f, 0.96f };
		AppendText(vertices, text, originX, originY, maxWidthPx, 2, color);
		if (vertices.empty())
		{
			return;
		}

		std::memcpy(m_mappedVertices, vertices.data(), vertices.size() * sizeof(GlyphVertex));

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = static_cast<float>(extent.height);
		viewport.width = static_cast<float>(extent.width);
		viewport.height = -static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		const bool useFont = m_fontGpuReady && m_fontPipeline != VK_NULL_HANDLE;
		if (useFont)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fontPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fontPipelineLayout, 0, 1, &m_fontDescriptorSet, 0, nullptr);
		}
		else
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		}
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
		PushConstants push{};
		push.viewportSize[0] = static_cast<float>(extent.width);
		push.viewportSize[1] = static_cast<float>(extent.height);
		if (useFont)
		{
			vkCmdPushConstants(cmd, m_fontPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		}
		else
		{
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		}
		vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
	}

	void AuthGlyphPass::RecordModel(VkDevice device, VkCommandBuffer cmd, VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model,
		const AuthUiTheme& theme)
	{
		if (!IsValid() || cmd == VK_NULL_HANDLE || m_mappedVertices == nullptr || extent.width == 0 || extent.height == 0 || !model.visible)
		{
			return;
		}

		std::vector<GlyphVertex> vertices;
		vertices.reserve(m_maxVertices);

		const AuthUiLayoutMetrics layout = BuildAuthUiLayoutMetrics(extent, state, model);
		const int32_t panelW = layout.panelW;
		const int32_t panelH = layout.panelH;
		const int32_t panelX = layout.panelX;
		const int32_t panelY = layout.panelY;
		const int32_t contentX = layout.contentX;
		const int32_t contentW = layout.contentW;

		const float titleColor[4] = { theme.text[0], theme.text[1], theme.text[2], 1.0f };
		const float mutedColor[4] = { theme.mutedText[0], theme.mutedText[1], theme.mutedText[2], 0.92f };
		const float accentColor[4] = { theme.accent[0], theme.accent[1], theme.accent[2], 1.0f };
		const float primaryColor[4] = { theme.primary[0], theme.primary[1], theme.primary[2], 1.0f };
		const float errorColor[4] = { 1.0f, 0.72f, 0.72f, 1.0f };
		const float bodyColor[4] = { theme.text[0], theme.text[1], theme.text[2], 0.94f };
		const int32_t topOffset = layout.topOffset;
		const int32_t bodyScale = std::clamp(panelW / 260, 2, 4);
		const bool bigTitle =
			state.languageSelection || state.languageOptions
			|| state.login || state.registerMode
			|| state.verifyEmail || state.forgotPassword
			|| state.characterCreate;
		const int32_t titleScale = std::clamp(bodyScale + (bigTitle ? 4 : 1), 6, 9);
		const int32_t smallScale = std::max(2, bodyScale - 1);
		const int32_t bodyLineStep = 7 * bodyScale + 2 * bodyScale;
		const int32_t fieldRowStep = layout.fieldRowStepPx;

		auto appendBlock = [this, &vertices](int32_t x, int32_t y, int32_t widthPx, int32_t heightPx, const float color[4])
		{
			if (vertices.size() + 6u > m_maxVertices || widthPx <= 0 || heightPx <= 0)
			{
				return;
			}
			uint32_t bits0 = 0;
			uint32_t bits1 = 0;
			PackRows({ 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F }, bits0, bits1);
			const float left = static_cast<float>(x);
			const float top = static_cast<float>(y);
			const float right = static_cast<float>(x + widthPx);
			const float bottom = static_cast<float>(y + heightPx);
			const GlyphVertex quad[6] = {
				{{ left, top },    { 0.0f, 0.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ right, top },   { 1.0f, 0.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ right, bottom },{ 1.0f, 1.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ left, top },    { 0.0f, 0.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ right, bottom },{ 1.0f, 1.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1},
				{{ left, bottom }, { 0.0f, 1.0f }, { color[0], color[1], color[2], color[3] }, bits0, bits1}
			};
			vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
		};
		auto appendCenteredText = [this, &vertices](std::string_view text, int32_t boxX, int32_t y, int32_t boxW, int32_t scale, const float color[4])
		{
			const int32_t textWidth = MeasureTextWidthPx(text, scale);
			const int32_t x = std::max(boxX, boxX + (boxW - textWidth) / 2);
			AppendText(vertices, text, x, y, boxW, scale, color);
		};

		// Titres: sur la colonne de contenu sauf page login minimal (artW=0) où on utilise toute la largeur.
		const bool drawTitleAcrossPanel = state.login && state.minimalChrome && !state.loginArtColumn;
		const int32_t titleAreaX = drawTitleAcrossPanel ? (panelX + 24) : contentX;
		const int32_t titleAreaW = drawTitleAcrossPanel ? (panelW - 48) : contentW;
		if (!model.titleLine2.empty())
		{
			AppendText(vertices, model.titleLine1, titleAreaX, panelY + 22, titleAreaW, titleScale, titleColor);
			AppendText(vertices, model.titleLine2, titleAreaX, panelY + 30 + titleScale * 10, titleAreaW, bodyScale, mutedColor);
		}
		else
		{
			AppendText(vertices, model.titleLine1, titleAreaX, panelY + 24, titleAreaW, titleScale, titleColor);
		}
		const bool centeredLanguageSelection = state.languageSelection || state.languageOptions;
		const int32_t sectionTopPad = centeredLanguageSelection ? 50 : (model.titleLine2.empty() ? 30 : 38);
		const int32_t sectionTitleY = panelY + sectionTopPad + titleScale * 14;
		if (centeredLanguageSelection)
		{
			appendCenteredText(model.sectionTitle, contentX, sectionTitleY, contentW, bodyScale, titleColor);
		}
		else
		{
			AppendText(vertices, model.sectionTitle, contentX, sectionTitleY, contentW, bodyScale, titleColor);
		}

		if (!model.infoBanner.empty())
		{
			// Aligner le texte de la bannière avec son fond (AuthUiRenderer) : topOffset réserve 44px
			// au-dessus du premier champ pour la bannière. sectionTitleY + 9 provoquait un chevauchement
			// avec le titre de section (sectionTitle occupe sectionTitleY à sectionTitleY + 7*bodyScale).
			const int32_t bannerY = panelY + topOffset - 38;
			AppendText(vertices, model.infoBanner, contentX + 10, bannerY, contentW - 20, bodyScale, titleColor);
		}

		if (state.submitting)
		{
			if (!model.bodyLines.empty())
			{
				const int32_t submitY = panelY + (!model.infoBanner.empty() ? 128 : 118);
				AppendText(vertices, model.bodyLines.front().text, contentX + 18, submitY + 84, contentW - 36, bodyScale, titleColor);
			}
		}
		else if (!model.errorText.empty())
		{
			AppendText(vertices, model.errorText, contentX + 18, panelY + 124, contentW - 36, bodyScale, titleColor);
			if (!model.actions.empty())
			{
				AppendText(vertices, model.actions.front().label, contentX + 12, panelY + 233, contentW - 24, bodyScale, titleColor);
			}
		}
		else if (state.terms)
		{
			int32_t metaY = panelY + 110;
			for (int32_t i = 0; i < std::min<int32_t>(4, static_cast<int32_t>(model.bodyLines.size())); ++i)
			{
				AppendText(vertices, model.bodyLines[static_cast<size_t>(i)].text, contentX + 10, metaY, contentW - 28, smallScale, mutedColor);
				metaY += bodyLineStep;
			}
			if (model.bodyLines.size() >= 5u)
			{
				const int32_t termsBoxY = panelY + 92;
				const int32_t termsBoxH = std::max(180, panelH - 210);
				const int32_t textY = termsBoxY + 88;
				const int32_t textH = std::max(56, termsBoxH - 140);
				const int32_t maxLines = std::max(3, textH / 18);
				std::string content = model.bodyLines[4].text;
				int32_t lineY = textY;
				int32_t lines = 0;
				size_t start = 0;
				while (start < content.size() && lines < maxLines)
				{
					size_t end = start;
					int32_t width = 0;
					size_t lastSpace = std::string::npos;
					while (end < content.size())
					{
						char ch = content[end];
						if (ch == ' ') lastSpace = end;
						if (ch == '\n') break;
						width += GlyphAdvancePx(ch, bodyScale);
						if (width > contentW - 32) break;
						++end;
					}
					size_t lineEnd = end;
					if (end < content.size() && content[end] != '\n' && width > contentW - 32 && lastSpace != std::string::npos && lastSpace > start)
					{
						lineEnd = lastSpace;
					}
					AppendText(vertices, std::string_view(content).substr(start, lineEnd - start), contentX + 12, lineY, contentW - 32, bodyScale, bodyColor);
					lineY += bodyLineStep;
					++lines;
					start = lineEnd;
					while (start < content.size() && (content[start] == ' ' || content[start] == '\n' || content[start] == '\r'))
					{
						++start;
					}
				}
			}
			if (model.bodyLines.size() >= 6u)
			{
				AppendText(vertices, model.bodyLines[5].text, contentX + 12, panelY + panelH - 114, contentW - 24, smallScale, mutedColor);
			}
			if (model.bodyLines.size() >= 7u)
			{
				const bool activeAck = model.bodyLines[6].active;
				AppendText(vertices, model.bodyLines[6].text, contentX + 12, panelY + panelH - 132, contentW - 24, bodyScale, activeAck ? titleColor : bodyColor);
			}
			if (!model.actions.empty())
			{
				const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
				const int32_t gapTerms = 10;
				const int32_t actionW = std::max(110, (contentW - (actionCount - 1) * gapTerms) / actionCount);
				const int32_t termsBtnY = panelY + panelH - 92 + (58 - kAuthUiActionButtonHeightPx) / 2;
				const int32_t termsLabelY = termsBtnY + (kAuthUiActionButtonHeightPx - bodyLineStep) / 2;
				for (int32_t i = 0; i < actionCount; ++i)
				{
					if (static_cast<size_t>(i) >= model.actions.size()) break;
					const int32_t x = contentX + i * (actionW + gapTerms);
					const std::string& label = model.actions[static_cast<size_t>(i)].label;
					const int32_t labelWidth = MeasureTextWidthPx(label, bodyScale);
					const int32_t labelX = std::max(x + 10, x + (actionW - labelWidth) / 2);
					AppendText(vertices, label, labelX, termsLabelY, actionW - 20, bodyScale, titleColor);
				}
			}
		}
		else
		{
			for (int32_t i = 0; i < static_cast<int32_t>(model.fields.size()); ++i)
			{
				const int32_t y = panelY + topOffset + i * fieldRowStep;
				const auto& field = model.fields[static_cast<size_t>(i)];
				AppendText(vertices, field.label, contentX + 10, y - (smallScale * 9), contentW / 2, smallScale, mutedColor);
				if (!field.tooltipText.empty())
				{
					AppendText(vertices, "(i)", std::max(contentX + 10, contentX + contentW - 36), y - (smallScale * 9), 28, smallScale, accentColor);
				}
				AppendText(vertices, field.value, contentX + 12, y + 8, contentW - 24, bodyScale, field.active ? titleColor : bodyColor);
				if (i == model.hoveredFieldInfoIndex && !field.tooltipText.empty())
				{
					AppendText(vertices, field.tooltipText, contentX + 8, y + 36, contentW - 16, smallScale, bodyColor);
				}
				if (field.active && !field.cyclePicker)
				{
					const int32_t caretX = contentX + 12 + MeasureTextWidthPx(field.value, bodyScale) + 2;
					appendBlock(std::min(caretX, contentX + contentW - 14), y + 7, std::max(2, bodyScale - 1), 7 * bodyScale + 2, accentColor);
				}
			}
			const int32_t bodyLinePitch = centeredLanguageSelection
				? std::max(36, bodyLineStep + 16)
				: std::max(28, bodyLineStep + 10);
			const int32_t afterFieldsGap = centeredLanguageSelection ? 34 : 18;
			const int32_t bodyStartY = panelY + topOffset + static_cast<int32_t>(model.fields.size()) * fieldRowStep + afterFieldsGap;
			for (int32_t i = 0; i < model.visibleBodyLineCount; ++i)
			{
				const auto& line = model.bodyLines[static_cast<size_t>(model.visibleBodyLineStart + i)];
				const int32_t y = bodyStartY + i * bodyLinePitch - 4;
				const float* lineColor = line.link
					? (line.hovered ? primaryColor : accentColor)
					: (line.active ? accentColor : (line.hovered ? primaryColor : bodyColor));
				if (line.checkbox)
				{
					const float borderMuted[4] = { mutedColor[0], mutedColor[1], mutedColor[2], 1.0f };
					appendBlock(contentX + 4, y - 2, 14, 14, borderMuted);
					appendBlock(contentX + 6, y, 10, 10, theme.background);
					if (line.checkboxChecked)
					{
						appendBlock(contentX + 8, y + 2, 6, 6, accentColor);
					}
					AppendText(vertices, line.text, contentX + 24, y, contentW - 30, bodyScale, lineColor);
					continue;
				}
				if (centeredLanguageSelection && model.visibleBodyLineStart == 0 && i <= 1)
				{
					appendCenteredText(line.text, contentX + 2, y, contentW - 6, bodyScale, lineColor);
				}
				else
				{
					AppendText(vertices, line.text, contentX + 2, y, contentW - 6, bodyScale, lineColor);
				}
			}
			const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
			const int32_t gap = 10;
			AuthLoginTwoRowLayout loginTwoRow{};
			const bool loginTwoRows = TryGetLoginTwoRowLayout(layout, state, model, loginTwoRow);
			if (loginTwoRows)
			{
				for (int32_t row = 0; row < 2; ++row)
				{
					const int32_t rowY = (row == 0) ? loginTwoRow.secondaryRowY : loginTwoRow.primaryRowY;
					int32_t colX = contentX;
					for (int32_t col = 0; col < 2; ++col)
					{
						const int32_t i = row * 2 + col;
						if (i >= actionCount || static_cast<size_t>(i) >= model.actions.size())
						{
							break;
						}
						int32_t btnW = loginTwoRow.buttonHalfWidth;
						int32_t x = contentX + col * (loginTwoRow.buttonHalfWidth + gap);
						if (row == 1)
						{
							btnW = (col == 0) ? loginTwoRow.primarySubmitWidth : loginTwoRow.primaryQuitWidth;
							x = (col == 0) ? contentX : (contentX + loginTwoRow.primarySubmitWidth + gap);
						}
						const auto& action = model.actions[static_cast<size_t>(i)];
						const int32_t labelWidth = MeasureTextWidthPx(action.label, bodyScale);
						const int32_t labelX = std::max(x + 8, x + (btnW - labelWidth) / 2);
						const int32_t labelY = rowY + (kAuthUiActionButtonHeightPx - bodyLineStep) / 2;
						// Les actions "emphasized" ont un fond de couleur proche de l’accent.
						// Si on dessine le texte en accentColor, il devient quasi illisible.
						const float* ac = action.primary ? titleColor : (action.emphasized ? titleColor : mutedColor);
						AppendText(vertices, action.label, labelX, labelY, btnW - 16, bodyScale, ac);
					}
				}
			}
			else
			{
				const int32_t buttonPadAfterBody = centeredLanguageSelection ? 28 : 20;
				const int32_t buttonY = std::min(panelY + panelH - 86, bodyStartY + model.visibleBodyLineCount * bodyLinePitch + buttonPadAfterBody);
				const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
				const int32_t singleRowLabelY = buttonY + (kAuthUiActionButtonHeightPx - bodyLineStep) / 2;
				for (int32_t i = 0; i < actionCount; ++i)
				{
					if (static_cast<size_t>(i) >= model.actions.size()) break;
					const int32_t x = contentX + i * (actionW + gap);
					const auto& action = model.actions[static_cast<size_t>(i)];
					const int32_t labelWidth = MeasureTextWidthPx(action.label, bodyScale);
					const int32_t labelX = std::max(x + 10, x + (actionW - labelWidth) / 2);
					const float* ac = action.primary ? titleColor : (action.emphasized ? titleColor : mutedColor);
					AppendText(vertices, action.label, labelX, singleRowLabelY, actionW - 20, bodyScale, ac);
				}
			}
			if (!model.errorText.empty())
			{
				AppendText(vertices, model.errorText, contentX + 14, panelY + 138, contentW - 28, bodyScale, errorColor);
			}
			if (!model.footerHint.empty())
			{
				AppendText(vertices, model.footerHint, contentX, panelY + panelH - 28, contentW, smallScale, mutedColor);
			}
		}

		if (vertices.empty())
		{
			return;
		}
		std::memcpy(m_mappedVertices, vertices.data(), vertices.size() * sizeof(GlyphVertex));
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = static_cast<float>(extent.height);
		viewport.width = static_cast<float>(extent.width);
		viewport.height = -static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		const bool useFont = m_fontGpuReady && m_fontPipeline != VK_NULL_HANDLE;
		if (useFont)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fontPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fontPipelineLayout, 0, 1, &m_fontDescriptorSet, 0, nullptr);
		}
		else
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		}
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
		PushConstants push{};
		push.viewportSize[0] = static_cast<float>(extent.width);
		push.viewportSize[1] = static_cast<float>(extent.height);
		if (useFont)
		{
			vkCmdPushConstants(cmd, m_fontPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		}
		else
		{
			vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		}
		vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
	}

	void AuthGlyphPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
		{
			return;
		}
		DestroyFontGpu(device);
		if (m_vertexMemory != VK_NULL_HANDLE)
		{
			if (m_mappedVertices != nullptr)
			{
				vkUnmapMemory(device, m_vertexMemory);
				m_mappedVertices = nullptr;
			}
			vkFreeMemory(device, m_vertexMemory, nullptr);
			m_vertexMemory = VK_NULL_HANDLE;
		}
		if (m_vertexBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_vertexBuffer, nullptr);
			m_vertexBuffer = VK_NULL_HANDLE;
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
		m_maxGlyphs = 0;
		m_maxVertices = 0;
	}
}
