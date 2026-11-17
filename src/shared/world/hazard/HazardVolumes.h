#pragma once

// M100.16 — Hazard Volumes : structures + sérialisation `instances/hazards.bin`.
//
// Sérialisation HEADER-ONLY (inline) : utilisable à la fois par engine_core
// (runtime/éditeur) et par zone_builder_lib (writer offline) sans dupliquer de
// symboles entre les deux libs. En-tête binaire de 24 octets, compatible avec
// la disposition de `engine::world::OutputVersionHeader` (magic, formatVersion,
// builderVersion, engineVersion, contentHash). Simulation strictement client.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::hazard
{
	enum class HazardType : uint32_t { Quicksand = 0, Bog = 1, Tar = 2, LavaSurface = 3 };
	enum class HazardShape : uint32_t { Box = 0, Cylinder = 1 };
	enum class EscapeMode : uint32_t
	{
		None = 0, MashButton = 1, LateralMove = 2, MashButtonRequireItem = 3
	};

	struct HazardVolume
	{
		HazardType  type = HazardType::Quicksand;
		HazardShape shape = HazardShape::Cylinder;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 boxHalfExtents{ 2.0f, 1.0f, 2.0f };
		float cylRadius = 4.0f;
		float cylHeight = 2.0f;
		float sinkRateMps = 0.15f;
		float maxDepthMeters = 1.8f;
		float slowdownMul = 0.10f;
		EscapeMode escapeMode = EscapeMode::MashButton;
		uint32_t requiredItemId = 0;
	};

	constexpr uint32_t kHazardsMagic = 0x5A415748u; // "HAZA"
	constexpr uint32_t kHazardsVersion = 1u;
	constexpr uint32_t kHazardsBuilderVersion = 1u;
	constexpr uint32_t kHazardsEngineVersion = 1u;

	/// Paramètres par défaut par type (cf. tableau M100.16).
	inline HazardVolume MakeDefaultHazard(HazardType type)
	{
		HazardVolume v;
		v.type = type;
		switch (type)
		{
			case HazardType::Quicksand:
				v.sinkRateMps = 0.15f; v.maxDepthMeters = 1.8f; v.slowdownMul = 0.10f;
				v.escapeMode = EscapeMode::MashButton; break;
			case HazardType::Bog:
				v.sinkRateMps = 0.08f; v.maxDepthMeters = 1.2f; v.slowdownMul = 0.20f;
				v.escapeMode = EscapeMode::LateralMove; break;
			case HazardType::Tar:
				v.sinkRateMps = 0.05f; v.maxDepthMeters = 0.8f; v.slowdownMul = 0.05f;
				v.escapeMode = EscapeMode::MashButtonRequireItem; break;
			case HazardType::LavaSurface:
				v.sinkRateMps = 0.0f; v.maxDepthMeters = 0.0f; v.slowdownMul = 0.0f;
				v.escapeMode = EscapeMode::None; break;
		}
		return v;
	}

	// ---- helpers little-endian (inline) ----
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

	/// Sérialise une liste de hazards en octets `hazards.bin`.
	inline std::vector<uint8_t> SaveHazardsBin(const std::vector<HazardVolume>& hazards)
	{
		std::vector<uint8_t> b;
		// En-tête 24 octets (compatible OutputVersionHeader).
		detail::PutU32(b, kHazardsMagic);
		detail::PutU32(b, kHazardsVersion);
		detail::PutU32(b, kHazardsBuilderVersion);
		detail::PutU32(b, kHazardsEngineVersion);
		detail::PutU64(b, 0ull); // contentHash (non calculé ici)
		detail::PutU32(b, static_cast<uint32_t>(hazards.size()));
		for (const HazardVolume& h : hazards)
		{
			detail::PutU32(b, static_cast<uint32_t>(h.type));
			detail::PutU32(b, static_cast<uint32_t>(h.shape));
			detail::PutF32(b, h.position.x); detail::PutF32(b, h.position.y); detail::PutF32(b, h.position.z);
			detail::PutF32(b, h.boxHalfExtents.x); detail::PutF32(b, h.boxHalfExtents.y); detail::PutF32(b, h.boxHalfExtents.z);
			detail::PutF32(b, h.cylRadius);
			detail::PutF32(b, h.cylHeight);
			detail::PutF32(b, h.sinkRateMps);
			detail::PutF32(b, h.maxDepthMeters);
			detail::PutF32(b, h.slowdownMul);
			detail::PutU32(b, static_cast<uint32_t>(h.escapeMode));
			detail::PutU32(b, h.requiredItemId);
		}
		return b;
	}

	/// Désérialise des octets `hazards.bin`. Renseigne `err` et renvoie false
	/// en cas d'en-tête/magic/version invalide ou de troncature.
	inline bool LoadHazardsBin(std::span<const uint8_t> bytes, std::vector<HazardVolume>& out, std::string& err)
	{
		size_t p = 0;
		uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kHazardsMagic) { err = "hazards.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kHazardsVersion) { err = "hazards.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder);
		detail::GetU32(bytes, p, engine);
		detail::GetU64(bytes, p, hash);
		uint32_t count = 0;
		if (!detail::GetU32(bytes, p, count)) { err = "hazards.bin: compteur manquant"; return false; }
		out.clear(); out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			HazardVolume h;
			uint32_t t = 0, s = 0, em = 0;
			if (!detail::GetU32(bytes, p, t)) { err = "hazards.bin: tronque"; return false; }
			detail::GetU32(bytes, p, s);
			detail::GetF32(bytes, p, h.position.x); detail::GetF32(bytes, p, h.position.y); detail::GetF32(bytes, p, h.position.z);
			detail::GetF32(bytes, p, h.boxHalfExtents.x); detail::GetF32(bytes, p, h.boxHalfExtents.y); detail::GetF32(bytes, p, h.boxHalfExtents.z);
			detail::GetF32(bytes, p, h.cylRadius);
			detail::GetF32(bytes, p, h.cylHeight);
			detail::GetF32(bytes, p, h.sinkRateMps);
			detail::GetF32(bytes, p, h.maxDepthMeters);
			detail::GetF32(bytes, p, h.slowdownMul);
			if (!detail::GetU32(bytes, p, em)) { err = "hazards.bin: tronque (escape)"; return false; }
			if (!detail::GetU32(bytes, p, h.requiredItemId)) { err = "hazards.bin: tronque (item)"; return false; }
			h.type = static_cast<HazardType>(t);
			h.shape = static_cast<HazardShape>(s);
			h.escapeMode = static_cast<EscapeMode>(em);
			out.push_back(h);
		}
		return true;
	}
}
