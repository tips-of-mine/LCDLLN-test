#include "engine/render/FontAtlasTtf.h"

#include "engine/core/Log.h"

#include <cmath>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace engine::render
{
	namespace
	{
		bool Utf8NextCodepoint(std::string_view s, size_t& i, int& cp)
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

	struct FontAtlasTtfData
	{
		std::vector<uint8_t> ttfCopy;
		std::vector<uint8_t> atlas;
		stbtt_packedchar ascii[96]{};
		stbtt_packedchar lat1[96]{};
		int w = 0;
		int h = 0;
		float pixelHeight = 16.f;
		int lineTopToBaselinePx = 12;
		int lineHeightPx = 16;
	};

	FontAtlasTtf::FontAtlasTtf() = default;
	FontAtlasTtf::~FontAtlasTtf() = default;
	FontAtlasTtf::FontAtlasTtf(FontAtlasTtf&&) noexcept = default;
	FontAtlasTtf& FontAtlasTtf::operator=(FontAtlasTtf&&) noexcept = default;

	bool FontAtlasTtf::BuildFromMemory(const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight, int atlasW, int atlasH)
	{
		m_data.reset();
		if (ttfBytes == nullptr || ttfSize == 0 || pixelHeight < 6.f || atlasW < 256 || atlasH < 256)
		{
			return false;
		}

		auto data = std::make_unique<FontAtlasTtfData>();
		data->ttfCopy.assign(ttfBytes, ttfBytes + ttfSize);

		stbtt_fontinfo info{};
		if (stbtt_InitFont(&info, data->ttfCopy.data(), stbtt_GetFontOffsetForIndex(data->ttfCopy.data(), 0)) == 0)
		{
			LOG_WARN(Render, "[FontAtlasTtf] stbtt_InitFont failed");
			return false;
		}

		int ascent = 0;
		int descent = 0;
		int lineGap = 0;
		stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
		const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
		data->lineTopToBaselinePx = static_cast<int>(std::lround(static_cast<float>(ascent) * scale));
		data->lineHeightPx = static_cast<int>(std::lround(static_cast<float>(ascent - descent + lineGap) * scale));
		if (data->lineHeightPx < 8)
		{
			data->lineHeightPx = static_cast<int>(pixelHeight);
		}

		data->atlas.assign(static_cast<size_t>(atlasW) * static_cast<size_t>(atlasH), 0u);

		stbtt_pack_context pc{};
		if (stbtt_PackBegin(&pc, data->atlas.data(), atlasW, atlasH, 0, 2, nullptr) == 0)
		{
			LOG_WARN(Render, "[FontAtlasTtf] stbtt_PackBegin failed");
			return false;
		}
		stbtt_PackSetOversampling(&pc, 1, 1);

		stbtt_pack_range ranges[2]{};
		ranges[0].font_size = pixelHeight;
		ranges[0].first_unicode_codepoint_in_range = 32;
		ranges[0].num_chars = 96;
		ranges[0].chardata_for_range = data->ascii;
		ranges[1].font_size = pixelHeight;
		ranges[1].first_unicode_codepoint_in_range = 160;
		ranges[1].num_chars = 96;
		ranges[1].chardata_for_range = data->lat1;

		const int packOk = stbtt_PackFontRanges(&pc, data->ttfCopy.data(), 0, ranges, 2);
		stbtt_PackEnd(&pc);
		if (packOk == 0)
		{
			LOG_WARN(Render, "[FontAtlasTtf] stbtt_PackFontRanges failed (atlas trop petit ?)");
			return false;
		}

		data->w = atlasW;
		data->h = atlasH;
		data->pixelHeight = pixelHeight;
		m_data = std::move(data);
		LOG_INFO(Render, "[FontAtlasTtf] OK atlas={}x{} pxHeight={:.1f}", atlasW, atlasH, pixelHeight);
		return true;
	}

	bool FontAtlasTtf::IsValid() const
	{
		return m_data && !m_data->atlas.empty() && m_data->w > 0 && m_data->h > 0;
	}

	int FontAtlasTtf::AtlasWidth() const { return m_data ? m_data->w : 0; }
	int FontAtlasTtf::AtlasHeight() const { return m_data ? m_data->h : 0; }
	float FontAtlasTtf::PixelHeight() const { return m_data ? m_data->pixelHeight : 0.f; }
	int FontAtlasTtf::LineTopToBaselinePx() const { return m_data ? m_data->lineTopToBaselinePx : 0; }
	int FontAtlasTtf::LineHeightPx() const { return m_data ? m_data->lineHeightPx : 0; }

	const std::vector<uint8_t>& FontAtlasTtf::AtlasR8() const
	{
		static const std::vector<uint8_t> kEmpty;
		return m_data ? m_data->atlas : kEmpty;
	}

	static const stbtt_packedchar* CharsAndIndex(const FontAtlasTtfData& d, int codepoint, int* charIndex)
	{
		if (codepoint >= 32 && codepoint < 128)
		{
			*charIndex = codepoint - 32;
			return d.ascii;
		}
		if (codepoint >= 160 && codepoint < 256)
		{
			*charIndex = codepoint - 160;
			return d.lat1;
		}
		*charIndex = 0;
		return d.ascii;
	}

	bool FontAtlasTtf::GetPackedQuad(int codepoint, float* penX, float baselineY, float* x0, float* y0, float* x1, float* y1,
		float* s0, float* t0, float* s1, float* t1, float* xadvance) const
	{
		if (!m_data)
		{
			return false;
		}
		int idx = 0;
		const stbtt_packedchar* chars = CharsAndIndex(*m_data, codepoint, &idx);
		float y = baselineY;
		stbtt_aligned_quad q{};
		stbtt_GetPackedQuad(chars, m_data->w, m_data->h, idx, penX, &y, &q, 1);
		*x0 = q.x0;
		*y0 = q.y0;
		*x1 = q.x1;
		*y1 = q.y1;
		*s0 = q.s0;
		*t0 = q.t0;
		*s1 = q.s1;
		*t1 = q.t1;
		*xadvance = chars[idx].xadvance;
		return true;
	}

	float FontAtlasTtf::GlyphXAdvance(int codepoint) const
	{
		if (!m_data)
		{
			return 0.f;
		}
		int idx = 0;
		const stbtt_packedchar* chars = CharsAndIndex(*m_data, codepoint, &idx);
		return chars[idx].xadvance;
	}

	int32_t FontAtlasTtf::MeasureWidthPx(std::string_view utf8) const
	{
		if (!m_data)
		{
			return 0;
		}
		float w = 0.f;
		size_t i = 0;
		while (i < utf8.size())
		{
			int cp = 0;
			if (!Utf8NextCodepoint(utf8, i, cp))
			{
				break;
			}
			if (cp == '\r')
			{
				continue;
			}
			if (cp == '\n')
			{
				break;
			}
			int idx = 0;
			const stbtt_packedchar* chars = CharsAndIndex(*m_data, cp, &idx);
			w += chars[idx].xadvance;
		}
		return static_cast<int32_t>(std::lround(w));
	}
}
