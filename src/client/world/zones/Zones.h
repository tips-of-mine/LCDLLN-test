#pragma once

// M100.28 — Zones de gameplay polygonales typées : struct + sérialisation
// `instances/zones.bin`. Header-only (partagé engine_core/zone_builder).

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::zones
{
	enum class ZoneType : uint32_t
	{
		SafeZone = 0, PvPZone = 1, RaidZone = 2, NoBuild = 3, QuestTrigger = 4, WeatherOverride = 5
	};

	struct GameplayZone
	{
		ZoneType type = ZoneType::SafeZone;
		std::string name;
		std::vector<engine::math::Vec3> polygon; // sommets xz
		float transitionMarginMeters = 5.0f;     // WeatherOverride
		uint32_t weatherType = 0;                 // WeatherOverride
		float weatherBlendT = 0.0f;               // WeatherOverride
		uint32_t questId = 0;                     // QuestTrigger
	};

	constexpr uint32_t kZonesMagic = 0x534E4F5Au; // "ZONS"
	constexpr uint32_t kZonesVersion = 1u;

	namespace detail
	{
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
		}
		inline void PutU64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
		inline void PutF32(std::vector<uint8_t>& b, float f) { uint32_t u; std::memcpy(&u, &f, 4); PutU32(b, u); }
		inline void PutStr(std::vector<uint8_t>& b, const std::string& s)
		{
			PutU32(b, static_cast<uint32_t>(s.size()));
			b.insert(b.end(), s.begin(), s.end());
		}
		inline bool GetU32(std::span<const uint8_t> b, size_t& p, uint32_t& o)
		{
			if (p + 4 > b.size()) return false;
			o = uint32_t(b[p]) | (uint32_t(b[p+1])<<8) | (uint32_t(b[p+2])<<16) | (uint32_t(b[p+3])<<24); p += 4; return true;
		}
		inline bool GetU64(std::span<const uint8_t> b, size_t& p, uint64_t& o)
		{
			if (p + 8 > b.size()) return false; o = 0; for (int i = 0; i < 8; ++i) o |= uint64_t(b[p+i]) << (8*i); p += 8; return true;
		}
		inline bool GetF32(std::span<const uint8_t> b, size_t& p, float& o) { uint32_t u; if (!GetU32(b, p, u)) return false; std::memcpy(&o, &u, 4); return true; }
		inline bool GetStr(std::span<const uint8_t> b, size_t& p, std::string& o)
		{
			uint32_t n = 0; if (!GetU32(b, p, n)) return false;
			if (p + n > b.size()) return false;
			o.assign(reinterpret_cast<const char*>(b.data() + p), n); p += n; return true;
		}
	}

	inline std::vector<uint8_t> SaveZonesBin(const std::vector<GameplayZone>& zones)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kZonesMagic); detail::PutU32(b, kZonesVersion);
		detail::PutU32(b, 1u); detail::PutU32(b, 1u); detail::PutU64(b, 0ull);
		detail::PutU32(b, static_cast<uint32_t>(zones.size()));
		for (const GameplayZone& z : zones)
		{
			detail::PutU32(b, static_cast<uint32_t>(z.type));
			detail::PutStr(b, z.name);
			detail::PutU32(b, static_cast<uint32_t>(z.polygon.size()));
			for (const auto& v : z.polygon) { detail::PutF32(b, v.x); detail::PutF32(b, v.y); detail::PutF32(b, v.z); }
			detail::PutF32(b, z.transitionMarginMeters);
			detail::PutU32(b, z.weatherType);
			detail::PutF32(b, z.weatherBlendT);
			detail::PutU32(b, z.questId);
		}
		return b;
	}

	inline bool LoadZonesBin(std::span<const uint8_t> bytes, std::vector<GameplayZone>& out, std::string& err)
	{
		size_t p = 0; uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kZonesMagic) { err = "zones.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kZonesVersion) { err = "zones.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder); detail::GetU32(bytes, p, engine); detail::GetU64(bytes, p, hash);
		uint32_t count = 0; if (!detail::GetU32(bytes, p, count)) { err = "zones.bin: compteur manquant"; return false; }
		out.clear(); out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			GameplayZone z; uint32_t t = 0;
			if (!detail::GetU32(bytes, p, t)) { err = "zones.bin: tronque"; return false; }
			z.type = static_cast<ZoneType>(t);
			if (!detail::GetStr(bytes, p, z.name)) { err = "zones.bin: nom tronque"; return false; }
			uint32_t vc = 0; if (!detail::GetU32(bytes, p, vc)) { err = "zones.bin: poly count"; return false; }
			z.polygon.reserve(vc);
			for (uint32_t k = 0; k < vc; ++k)
			{
				engine::math::Vec3 v; detail::GetF32(bytes, p, v.x); detail::GetF32(bytes, p, v.y);
				if (!detail::GetF32(bytes, p, v.z)) { err = "zones.bin: poly tronque"; return false; }
				z.polygon.push_back(v);
			}
			detail::GetF32(bytes, p, z.transitionMarginMeters);
			detail::GetU32(bytes, p, z.weatherType);
			detail::GetF32(bytes, p, z.weatherBlendT);
			if (!detail::GetU32(bytes, p, z.questId)) { err = "zones.bin: champs tronques"; return false; }
			out.push_back(std::move(z));
		}
		return true;
	}
}
