#pragma once

// M100.20 — Zones de vent locales : struct + sérialisation `instances/wind_zones.bin`.
// Header-only (partagé engine_core/zone_builder). En-tête 24 octets.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::wind
{
	struct WindZone
	{
		std::vector<engine::math::Vec3> polygon; // sommets xz (y ignoré)
		float directionX = 1.0f;
		float directionZ = 0.0f;
		float forceMps = 4.0f;
		float turbulenceFreq = 0.2f;
		float turbulenceAmp = 0.5f;
	};

	constexpr uint32_t kWindZonesMagic = 0x444E4957u; // "WIND"
	constexpr uint32_t kWindZonesVersion = 1u;

	namespace detail
	{
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
			b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
		}
		inline void PutU64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
		inline void PutF32(std::vector<uint8_t>& b, float f) { uint32_t u; std::memcpy(&u, &f, 4); PutU32(b, u); }
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
		inline bool GetF32(std::span<const uint8_t> b, size_t& p, float& o)
		{
			uint32_t u; if (!GetU32(b, p, u)) return false; std::memcpy(&o, &u, 4); return true;
		}
	}

	inline std::vector<uint8_t> SaveWindZonesBin(const std::vector<WindZone>& zones)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kWindZonesMagic);
		detail::PutU32(b, kWindZonesVersion);
		detail::PutU32(b, 1u); // builder
		detail::PutU32(b, 1u); // engine
		detail::PutU64(b, 0ull);
		detail::PutU32(b, static_cast<uint32_t>(zones.size()));
		for (const WindZone& z : zones)
		{
			detail::PutU32(b, static_cast<uint32_t>(z.polygon.size()));
			for (const auto& v : z.polygon) { detail::PutF32(b, v.x); detail::PutF32(b, v.y); detail::PutF32(b, v.z); }
			detail::PutF32(b, z.directionX); detail::PutF32(b, z.directionZ);
			detail::PutF32(b, z.forceMps);
			detail::PutF32(b, z.turbulenceFreq);
			detail::PutF32(b, z.turbulenceAmp);
		}
		return b;
	}

	inline bool LoadWindZonesBin(std::span<const uint8_t> bytes, std::vector<WindZone>& out, std::string& err)
	{
		size_t p = 0; uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kWindZonesMagic) { err = "wind_zones.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kWindZonesVersion) { err = "wind_zones.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder); detail::GetU32(bytes, p, engine); detail::GetU64(bytes, p, hash);
		uint32_t count = 0;
		if (!detail::GetU32(bytes, p, count)) { err = "wind_zones.bin: compteur manquant"; return false; }
		out.clear(); out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			WindZone z; uint32_t vc = 0;
			if (!detail::GetU32(bytes, p, vc)) { err = "wind_zones.bin: tronque"; return false; }
			z.polygon.reserve(vc);
			for (uint32_t k = 0; k < vc; ++k)
			{
				engine::math::Vec3 v;
				detail::GetF32(bytes, p, v.x); detail::GetF32(bytes, p, v.y);
				if (!detail::GetF32(bytes, p, v.z)) { err = "wind_zones.bin: tronque (poly)"; return false; }
				z.polygon.push_back(v);
			}
			detail::GetF32(bytes, p, z.directionX); detail::GetF32(bytes, p, z.directionZ);
			detail::GetF32(bytes, p, z.forceMps);
			detail::GetF32(bytes, p, z.turbulenceFreq);
			if (!detail::GetF32(bytes, p, z.turbulenceAmp)) { err = "wind_zones.bin: tronque (params)"; return false; }
			out.push_back(std::move(z));
		}
		return true;
	}
}
