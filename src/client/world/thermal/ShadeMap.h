#pragma once

// M100.27 — Shade map par chunk (couvert arboré, 64×64 R8). Format header-only
// `chunks/chunk_i_j/shade.bin`. En-tête 24 octets + résolution + octets de couverture.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace engine::world::thermal
{
	constexpr uint32_t kShadeMapMagic = 0x44484153u;   // "SHAD"
	constexpr uint32_t kShadeMapVersion = 1u;
	constexpr uint32_t kShadeMapResolution = 64u;      // par chunk

	struct ShadeMap
	{
		uint32_t resolution = kShadeMapResolution;
		std::vector<uint8_t> coverage; // resolution*resolution, 0..255 (255 = canopée pleine)

		uint8_t At(uint32_t x, uint32_t y) const
		{
			if (x >= resolution || y >= resolution) return 0;
			const size_t idx = static_cast<size_t>(y) * resolution + x;
			return (idx < coverage.size()) ? coverage[idx] : 0;
		}
	};

	namespace detail
	{
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
			b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
		}
		inline void PutU64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
		inline bool GetU32(std::span<const uint8_t> b, size_t& p, uint32_t& o)
		{
			if (p + 4 > b.size()) return false;
			o = uint32_t(b[p]) | (uint32_t(b[p+1])<<8) | (uint32_t(b[p+2])<<16) | (uint32_t(b[p+3])<<24);
			p += 4; return true;
		}
		inline bool GetU64(std::span<const uint8_t> b, size_t& p, uint64_t& o)
		{
			if (p + 8 > b.size()) return false;
			o = 0; for (int i = 0; i < 8; ++i) o |= uint64_t(b[p+i]) << (8*i); p += 8; return true;
		}
	}

	inline std::vector<uint8_t> SaveShadeMapBin(const ShadeMap& map)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kShadeMapMagic);
		detail::PutU32(b, kShadeMapVersion);
		detail::PutU32(b, 1u); // builder
		detail::PutU32(b, 1u); // engine
		detail::PutU64(b, 0ull);
		detail::PutU32(b, map.resolution);
		const size_t expected = static_cast<size_t>(map.resolution) * map.resolution;
		for (size_t i = 0; i < expected; ++i)
			b.push_back(i < map.coverage.size() ? map.coverage[i] : 0);
		return b;
	}

	inline bool LoadShadeMapBin(std::span<const uint8_t> bytes, ShadeMap& out, std::string& err)
	{
		size_t p = 0; uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kShadeMapMagic) { err = "shade.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kShadeMapVersion) { err = "shade.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder); detail::GetU32(bytes, p, engine); detail::GetU64(bytes, p, hash);
		if (!detail::GetU32(bytes, p, out.resolution)) { err = "shade.bin: resolution manquante"; return false; }
		const size_t expected = static_cast<size_t>(out.resolution) * out.resolution;
		if (p + expected > bytes.size()) { err = "shade.bin: donnees tronquees"; return false; }
		out.coverage.assign(bytes.begin() + static_cast<long>(p), bytes.begin() + static_cast<long>(p + expected));
		return true;
	}
}
