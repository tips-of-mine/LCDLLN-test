#pragma once

// Auberge éditable — entité « Building » : struct + sérialisation
// `instances/zone_<id>/buildings.bin` (magic « LCBD » v1).
//
// Un Building est une grappe d'éléments existants (murs, toits, portes,
// mobilier) regroupée en une entité logique avec un transform de groupe.
// Chaque pièce (`BuildingPart`) porte son chemin glTF EN CLAIR (pas de hash
// `assetId`) + un transform LOCAL relatif à l'origine du bâtiment. Le client
// peut donc rendre directement, sans résoudre de hash.
//
// Sérialisation HEADER-ONLY (inline), comme `PropInstances.h` : partagée
// engine_core (runtime/éditeur) sans dupliquer de symboles. Réutilise les
// helpers `detail::Put*/Get*` de PropInstances.h.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "src/client/world/instances/PropInstances.h"
#include "src/shared/math/Math.h"

namespace engine::world::instances
{
	/// Une pièce d'un bâtiment, en espace LOCAL (relatif à l'origine du Building).
	struct BuildingPart
	{
		std::string        gltfRelativePath;            // ex: "meshes/props/Wall_Plaster_Straight.gltf"
		engine::math::Vec3 localPosition{ 0.0f, 0.0f, 0.0f }; // offset local (m)
		engine::math::Vec3 localEulerDeg{ 0.0f, 0.0f, 0.0f }; // rotation XYZ locale (degrés)
		float              localScale = 1.0f;           // échelle uniforme locale
	};

	/// Un bâtiment = grappe de pièces + transform de groupe. `guid` 0 = invalide.
	struct BuildingInstance
	{
		uint64_t                  guid = 0u;
		std::string               displayName;          // libre, pour l'Outliner ("Auberge")
		engine::math::Vec3        worldPosition{ 0.0f, 0.0f, 0.0f }; // origine du groupe (m)
		float                     worldYawDeg = 0.0f;   // yaw du groupe (degrés)
		float                     worldScale  = 1.0f;   // échelle uniforme du groupe
		std::vector<BuildingPart> parts;
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

	/// Sérialise la liste de bâtiments au format `buildings.bin` (LCBD v1).
	/// Header 16 octets : [magic(4)][version(4)][buildingCount(4)][reserved(4)].
	inline std::vector<uint8_t> SaveBuildingsBin(const std::vector<BuildingInstance>& buildings)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kBuildingsMagic);
		detail::PutU32(b, kBuildingsVersion);
		detail::PutU32(b, static_cast<uint32_t>(buildings.size()));
		detail::PutU32(b, 0u); // reserved
		for (const BuildingInstance& bd : buildings)
		{
			detail::PutU64(b, bd.guid);
			detail::PutStr(b, bd.displayName);
			detail::PutVec3(b, bd.worldPosition);
			detail::PutF32(b, bd.worldYawDeg);
			detail::PutF32(b, bd.worldScale);
			detail::PutU32(b, static_cast<uint32_t>(bd.parts.size()));
			for (const BuildingPart& pt : bd.parts)
			{
				detail::PutStr(b, pt.gltfRelativePath);
				detail::PutVec3(b, pt.localPosition);
				detail::PutVec3(b, pt.localEulerDeg);
				detail::PutF32(b, pt.localScale);
			}
		}
		return b;
	}

	/// Désérialise un `buildings.bin`. Valide magic + version. Reset `out`.
	/// Fichier vide (buildingCount = 0) : retourne true, liste vide.
	inline bool LoadBuildingsBin(std::span<const uint8_t> bytes,
		std::vector<BuildingInstance>& out, std::string& err)
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
			BuildingInstance bd;
			if (!detail::GetU64(bytes, p, bd.guid))         { err = "buildings.bin: guid tronque"; return false; }
			if (!detail::GetStr(bytes, p, bd.displayName))  { err = "buildings.bin: nom tronque"; return false; }
			if (!detail::GetVec3(bytes, p, bd.worldPosition)) { err = "buildings.bin: pos tronquee"; return false; }
			if (!detail::GetF32(bytes, p, bd.worldYawDeg))  { err = "buildings.bin: yaw tronque"; return false; }
			if (!detail::GetF32(bytes, p, bd.worldScale))   { err = "buildings.bin: scale tronque"; return false; }
			uint32_t partCount = 0;
			if (!detail::GetU32(bytes, p, partCount))       { err = "buildings.bin: partCount tronque"; return false; }
			bd.parts.reserve(partCount);
			for (uint32_t j = 0; j < partCount; ++j)
			{
				BuildingPart pt;
				if (!detail::GetStr(bytes, p, pt.gltfRelativePath)) { err = "buildings.bin: part path tronque"; return false; }
				if (!detail::GetVec3(bytes, p, pt.localPosition))   { err = "buildings.bin: part pos tronquee"; return false; }
				if (!detail::GetVec3(bytes, p, pt.localEulerDeg))   { err = "buildings.bin: part rot tronquee"; return false; }
				if (!detail::GetF32(bytes, p, pt.localScale))       { err = "buildings.bin: part scale tronque"; return false; }
				bd.parts.push_back(std::move(pt));
			}
			out.push_back(std::move(bd));
		}
		return true;
	}
}
