#pragma once

// M100.18 — Foliage instances : struct + sérialisation `instances/foliage.bin`.
//
// Sérialisation HEADER-ONLY (inline), partagée engine_core/zone_builder sans
// doublon de symboles. Positions chunk-local + assetIdHash + rotationY + scale.
// En-tête 24 octets compatible OutputVersionHeader. Strictement éditeur→client.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::foliage
{
	struct FoliageInstance
	{
		uint32_t assetIdHash = 0;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f }; // chunk-local
		float rotationY = 0.0f;
		float scale = 1.0f;
	};

	constexpr uint32_t kFoliageMagic = 0x47494C46u;   // "FLIG"
	constexpr uint32_t kFoliageVersion = 1u;
	constexpr uint32_t kFoliageBuilderVersion = 1u;
	constexpr uint32_t kFoliageEngineVersion = 1u;
	constexpr uint32_t kFoliageDensityResolution = 1025u; // par chunk

	namespace detail
	{
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
			b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
		}
		inline void PutU64(std::vector<uint8_t>& b, uint64_t v)
		{
			for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i)));
		}
		inline void PutF32(std::vector<uint8_t>& b, float f)
		{
			uint32_t u; std::memcpy(&u, &f, 4); PutU32(b, u);
		}
		inline bool GetU32(std::span<const uint8_t> b, size_t& p, uint32_t& out)
		{
			if (p + 4 > b.size()) return false;
			out = uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) |
			      (uint32_t(b[p + 3]) << 24);
			p += 4; return true;
		}
		inline bool GetU64(std::span<const uint8_t> b, size_t& p, uint64_t& out)
		{
			if (p + 8 > b.size()) return false;
			out = 0; for (int i = 0; i < 8; ++i) out |= uint64_t(b[p + i]) << (8 * i);
			p += 8; return true;
		}
		inline bool GetF32(std::span<const uint8_t> b, size_t& p, float& out)
		{
			uint32_t u; if (!GetU32(b, p, u)) return false;
			std::memcpy(&out, &u, 4); return true;
		}
	}

	inline std::vector<uint8_t> SaveFoliageBin(const std::vector<FoliageInstance>& items)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kFoliageMagic);
		detail::PutU32(b, kFoliageVersion);
		detail::PutU32(b, kFoliageBuilderVersion);
		detail::PutU32(b, kFoliageEngineVersion);
		detail::PutU64(b, 0ull);
		detail::PutU32(b, static_cast<uint32_t>(items.size()));
		for (const FoliageInstance& f : items)
		{
			detail::PutU32(b, f.assetIdHash);
			detail::PutF32(b, f.position.x); detail::PutF32(b, f.position.y); detail::PutF32(b, f.position.z);
			detail::PutF32(b, f.rotationY);
			detail::PutF32(b, f.scale);
		}
		return b;
	}

	inline bool LoadFoliageBin(std::span<const uint8_t> bytes, std::vector<FoliageInstance>& out, std::string& err)
	{
		size_t p = 0;
		uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kFoliageMagic) { err = "foliage.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kFoliageVersion) { err = "foliage.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder);
		detail::GetU32(bytes, p, engine);
		detail::GetU64(bytes, p, hash);
		uint32_t count = 0;
		if (!detail::GetU32(bytes, p, count)) { err = "foliage.bin: compteur manquant"; return false; }
		out.clear(); out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			FoliageInstance f;
			if (!detail::GetU32(bytes, p, f.assetIdHash)) { err = "foliage.bin: tronque"; return false; }
			detail::GetF32(bytes, p, f.position.x); detail::GetF32(bytes, p, f.position.y); detail::GetF32(bytes, p, f.position.z);
			detail::GetF32(bytes, p, f.rotationY);
			if (!detail::GetF32(bytes, p, f.scale)) { err = "foliage.bin: tronque (scale)"; return false; }
			out.push_back(f);
		}
		return true;
	}
}
