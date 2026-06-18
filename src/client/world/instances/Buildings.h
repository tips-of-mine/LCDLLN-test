#pragma once

// Placements de bâtiments dans une zone : RÉFÉRENCES légères vers la
// bibliothèque de types (cf. BuildingTemplates.h). La carte ne stocke PAS les
// pièces : seulement (type + variante + transform monde). Le jeu résout chaque
// placement contre `BuildingTemplateLibrary` pour récupérer les meshes et
// afficher. Modifier une variante dans la bibliothèque met à jour tous les
// placements de ce type.
//
// Sérialisation HEADER-ONLY (inline) `instances/zone_<id>/buildings.bin`
// (magic « LCBD » v1), sur le patron de PropInstances.h.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "src/client/world/instances/PropInstances.h"
#include "src/shared/math/Math.h"

namespace engine::world::instances
{
	/// Un bâtiment posé sur la carte = référence vers une variante de la
	/// bibliothèque + transform de groupe. `guid` 0 = invalide.
	struct BuildingPlacement
	{
		uint64_t           guid = 0u;
		std::string        templateType;  // ex: "tavern" (fichier buildings/templates/tavern.json)
		std::string        variantId;     // ex: "auberge_terrasse"
		std::string        displayName;   // libre, pour l'Outliner (défaut = variantId)
		engine::math::Vec3 worldPosition{ 0.0f, 0.0f, 0.0f }; // origine du groupe (m)
		float              worldYawDeg = 0.0f; // yaw du groupe (degrés)
		float              worldScale  = 1.0f; // échelle uniforme du groupe
	};

	constexpr uint32_t kBuildingsMagic   = 0x4442434Cu; // "LCBD" little-endian
	constexpr uint32_t kBuildingsVersion = 1u;

	namespace detail
	{
		inline void PutU16(std::vector<uint8_t>& b, uint16_t v)
		{
			b.push_back(uint8_t(v & 0xFFu));
			b.push_back(uint8_t((v >> 8) & 0xFFu));
		}
		inline void PutStr(std::vector<uint8_t>& b, const std::string& s)
		{
			const uint16_t len = static_cast<uint16_t>(s.size() > 0xFFFFu ? 0xFFFFu : s.size());
			PutU16(b, len);
			b.insert(b.end(), s.begin(), s.begin() + len);
		}
		inline bool GetU16(std::span<const uint8_t> b, size_t& p, uint16_t& out)
		{
			if (p + 2 > b.size()) return false;
			out = uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
			p += 2; return true;
		}
		inline bool GetStr(std::span<const uint8_t> b, size_t& p, std::string& out)
		{
			uint16_t len = 0;
			if (!GetU16(b, p, len)) return false;
			if (p + len > b.size()) return false;
			out.assign(reinterpret_cast<const char*>(b.data() + p), len);
			p += len; return true;
		}
		inline void PutVec3(std::vector<uint8_t>& b, const engine::math::Vec3& v)
		{
			PutF32(b, v.x); PutF32(b, v.y); PutF32(b, v.z);
		}
		inline bool GetVec3(std::span<const uint8_t> b, size_t& p, engine::math::Vec3& v)
		{
			return GetF32(b, p, v.x) && GetF32(b, p, v.y) && GetF32(b, p, v.z);
		}
	}

	/// Sérialise les placements au format `buildings.bin` (LCBD v1).
	/// Header 16 octets : [magic(4)][version(4)][placementCount(4)][reserved(4)].
	inline std::vector<uint8_t> SaveBuildingsBin(const std::vector<BuildingPlacement>& placements)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kBuildingsMagic);
		detail::PutU32(b, kBuildingsVersion);
		detail::PutU32(b, static_cast<uint32_t>(placements.size()));
		detail::PutU32(b, 0u); // reserved
		for (const BuildingPlacement& pl : placements)
		{
			detail::PutU64(b, pl.guid);
			detail::PutStr(b, pl.templateType);
			detail::PutStr(b, pl.variantId);
			detail::PutStr(b, pl.displayName);
			detail::PutVec3(b, pl.worldPosition);
			detail::PutF32(b, pl.worldYawDeg);
			detail::PutF32(b, pl.worldScale);
		}
		return b;
	}

	/// Désérialise un `buildings.bin`. Valide magic + version. Reset `out`.
	inline bool LoadBuildingsBin(std::span<const uint8_t> bytes,
		std::vector<BuildingPlacement>& out, std::string& err)
	{
		out.clear();
		size_t p = 0;
		uint32_t magic = 0, version = 0, count = 0, reserved = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kBuildingsMagic)
		{
			err = "buildings.bin: magic invalide (LCBD attendu)"; return false;
		}
		if (!detail::GetU32(bytes, p, version) || version == 0u || version > kBuildingsVersion)
		{
			err = "buildings.bin: version non supportee"; return false;
		}
		if (!detail::GetU32(bytes, p, count) || !detail::GetU32(bytes, p, reserved))
		{
			err = "buildings.bin: header tronque"; return false;
		}
		out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			BuildingPlacement pl;
			if (!detail::GetU64(bytes, p, pl.guid))           { err = "buildings.bin: guid tronque"; return false; }
			if (!detail::GetStr(bytes, p, pl.templateType))   { err = "buildings.bin: type tronque"; return false; }
			if (!detail::GetStr(bytes, p, pl.variantId))      { err = "buildings.bin: variante tronquee"; return false; }
			if (!detail::GetStr(bytes, p, pl.displayName))    { err = "buildings.bin: nom tronque"; return false; }
			if (!detail::GetVec3(bytes, p, pl.worldPosition)) { err = "buildings.bin: pos tronquee"; return false; }
			if (!detail::GetF32(bytes, p, pl.worldYawDeg))    { err = "buildings.bin: yaw tronque"; return false; }
			if (!detail::GetF32(bytes, p, pl.worldScale))     { err = "buildings.bin: scale tronque"; return false; }
			out.push_back(std::move(pl));
		}
		return true;
	}
}
