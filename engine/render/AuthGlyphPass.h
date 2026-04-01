#pragma once

#include "engine/client/AuthUi.h"
#include "engine/render/AuthUiRenderer.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace engine::render
{
	class AuthGlyphPass
	{
	public:
		AuthGlyphPass() = default;
		AuthGlyphPass(const AuthGlyphPass&) = delete;
		AuthGlyphPass& operator=(const AuthGlyphPass&) = delete;

		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat colorFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxGlyphs = 8192,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		void Record(VkDevice device, VkCommandBuffer cmd, VkExtent2D extent,
			std::string_view text,
			int32_t originX, int32_t originY,
			int32_t maxWidthPx);

		void RecordModel(VkDevice device, VkCommandBuffer cmd, VkExtent2D extent,
			const engine::client::AuthUiPresenter::VisualState& state,
			const engine::client::AuthUiPresenter::RenderModel& model,
			const AuthUiTheme& theme);

		void Destroy(VkDevice device);

		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		void AppendText(std::vector<GlyphVertex>& vertices,
			std::string_view text,
			int32_t originX, int32_t originY,
			int32_t maxWidthPx,
			int32_t scale,
			const float color[4]) const;

		struct GlyphVertex
		{
			float pos[2];
			float uv[2];
			float color[4];
			uint32_t bits0 = 0;
			uint32_t bits1 = 0;
		};

		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
		void* m_mappedVertices = nullptr;
		uint32_t m_maxGlyphs = 0;
		uint32_t m_maxVertices = 0;
	};
}
