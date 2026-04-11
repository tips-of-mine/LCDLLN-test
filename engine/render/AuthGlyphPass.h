#pragma once

#include "engine/client/AuthUi.h"
#include "engine/render/AuthUiRenderer.h"
#include "engine/render/FontAtlasTtf.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace engine::render
{
	class AuthGlyphPass
	{
	public:
		AuthGlyphPass() = default;
		AuthGlyphPass(const AuthGlyphPass&) = delete;
		AuthGlyphPass& operator=(const AuthGlyphPass&) = delete;

		/// Si \p fragTtfSpirv est non nul, prépare un second pipeline + descriptor pour texte texturé (TTF).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat colorFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxGlyphs = 8192,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE,
			const uint32_t* fragTtfSpirv = nullptr,
			size_t fragTtfWordCount = 0);

		/// Construit l’atlas CPU (stb) et l’upload en R8 sur le GPU. Nécessite Init(..., fragTtf).
		bool UploadUiFontFromTtf(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, uint32_t queueFamilyIndex,
			const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight);

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
		bool HasUiFont() const { return m_fontGpuReady; }

		/// Charge la police de valeurs de champs (ex. Morpheus.ttf). Nécessite Init() appelé avant.
		bool UploadValueFontFromTtf(VkDevice device, VkPhysicalDevice physicalDevice,
			VkQueue graphicsQueue, uint32_t queueFamilyIndex,
			const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight);

		bool HasValueFont() const { return m_valueFontGpuReady; }

		/// Largeur texte pour centrage (bitmap ou TTF selon chargement).
		int32_t MeasureTextWidthPx(std::string_view text, int32_t scale) const;

	private:
		struct GlyphVertex
		{
			float pos[2];
			float uv[2];
			float color[4];
			uint32_t bits0 = 0;
			uint32_t bits1 = 0;
		};

		void AppendText(std::vector<GlyphVertex>& vertices,
			std::string_view text,
			int32_t originX, int32_t originY,
			int32_t maxWidthPx,
			int32_t scale,
			const float color[4],
			bool useValueFont = false) const;

		void AppendTextTtf(std::vector<GlyphVertex>& vertices,
			std::string_view text,
			int32_t originX, int32_t originY,
			int32_t maxWidthPx,
			int32_t scale,
			const float color[4],
			const FontAtlasTtf& atlas) const;

		void DestroyFontGpu(VkDevice device);

		bool CreateFontPipeline(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragTtfSpirv, size_t fragTtfWordCount,
			VkPipelineCache pipelineCache);

		/// Méthode interne partagée : construit un atlas TTF, upload l'image R8 sur le GPU,
		/// et met à jour le descriptor set fourni.
		/// Requiert que m_fontDescriptorSetLayout, le sampler et le descriptorSet soient déjà alloués.
		bool UploadFontAtlasToGpu(
			FontAtlasTtf& atlas,
			const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight,
			VkDevice device, VkPhysicalDevice physicalDevice,
			VkQueue graphicsQueue, uint32_t queueFamilyIndex,
			VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView,
			VkSampler outSampler,
			VkDescriptorSet outDescriptorSet);

		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
		void* m_mappedVertices = nullptr;
		uint32_t m_maxGlyphs = 0;
		uint32_t m_maxVertices = 0;

		FontAtlasTtf m_uiFont{};
		bool m_fontGpuReady = false;
		VkDescriptorSetLayout m_fontDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_fontDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_fontDescriptorSet = VK_NULL_HANDLE;
		VkSampler m_fontSampler = VK_NULL_HANDLE;
		VkPipelineLayout m_fontPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_fontPipeline = VK_NULL_HANDLE;
		VkImage m_fontImage = VK_NULL_HANDLE;
		VkDeviceMemory m_fontImageMemory = VK_NULL_HANDLE;
		VkImageView m_fontImageView = VK_NULL_HANDLE;

		// --- Second atlas TTF (police valeurs de champs, ex. Morpheus.ttf) ---
		// Partage m_fontPipeline / m_fontPipelineLayout / m_fontDescriptorSetLayout
		// (mêmes shaders, même vertex format). Seuls le descriptor set + image diffèrent.
		FontAtlasTtf   m_valueFont{};
		bool           m_valueFontGpuReady    = false;
		VkDescriptorPool m_valueFontDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet  m_valueFontDescriptorSet  = VK_NULL_HANDLE;
		VkSampler        m_valueFontSampler        = VK_NULL_HANDLE;
		VkImage          m_valueFontImage           = VK_NULL_HANDLE;
		VkDeviceMemory   m_valueFontImageMemory     = VK_NULL_HANDLE;
		VkImageView      m_valueFontImageView       = VK_NULL_HANDLE;
	};
}
