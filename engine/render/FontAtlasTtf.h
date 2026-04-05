#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace engine::render
{
	struct FontAtlasTtfData;

	/// Atlas R8 + métriques (stb_truetype) pour le texte UI. Plages : U+0020–007F, U+00A0–00FF.
	class FontAtlasTtf final
	{
	public:
		FontAtlasTtf();
		~FontAtlasTtf();
		FontAtlasTtf(FontAtlasTtf&&) noexcept;
		FontAtlasTtf& operator=(FontAtlasTtf&&) noexcept;
		FontAtlasTtf(const FontAtlasTtf&) = delete;
		FontAtlasTtf& operator=(const FontAtlasTtf&) = delete;

		bool BuildFromMemory(const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight, int atlasW = 1024, int atlasH = 1024);

		bool IsValid() const;
		int AtlasWidth() const;
		int AtlasHeight() const;
		float PixelHeight() const;
		int LineTopToBaselinePx() const;
		int LineHeightPx() const;
		const std::vector<uint8_t>& AtlasR8() const;

		int32_t MeasureWidthPx(std::string_view utf8) const;
		/// Avance horizontale (pixels bake) pour le codepoint, ou espace si absent de l’atlas.
		float GlyphXAdvance(int codepoint) const;

		/// Pour AuthGlyphPass : quad en espace écran + UV ; \p penX avancé ; \p baselineY fixe par ligne.
		bool GetPackedQuad(int codepoint, float* penX, float baselineY, float* x0, float* y0, float* x1, float* y1,
			float* s0, float* t0, float* s1, float* t1, float* xadvance) const;

	private:
		std::unique_ptr<FontAtlasTtfData> m_data;
	};
}
