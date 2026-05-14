#pragma once

#include "src/shared/math/Math.h"

#include <string>
#include <vector>

namespace engine::editor::world::volumes::overhangs
{
	/// Entrée du catalogue d'overhangs (M100.41). Décrit un mesh glTF
	/// disponible pour placement contre une falaise + métadonnées de
	/// pivot et d'aire de couverture.
	///
	/// Convention orientation : l'instance est placée tel que
	/// `wallAnchorPoint` (pivot-relatif) coïncide avec le point cliqué
	/// sur la falaise, et la rotation Y est ajustée pour aligner
	/// `wallNormalDirection` (vecteur pivot-relatif sortant) avec la
	/// normale terrain locale (horizontale, projection XZ). Le scale
	/// vertical de l'overhang reste contrôlé manuellement.
	struct OverhangCatalogEntry
	{
		std::string        id;                  // ex: "overhang_small_01"
		std::string        gltfRelativePath;    // ex: "meshes/overhangs/overhang_small_01.gltf"
		std::string        displayName;         // ex: "Petit surplomb"
		std::string        thumbnailPath;
		engine::math::Vec3 aabbMin;
		engine::math::Vec3 aabbMax;
		engine::math::Vec3 wallAnchorPoint;     // pivot-relatif, contact falaise
		engine::math::Vec3 wallNormalDirection; // pivot-relatif, vers le vide
		float              coverageRadius = 4.0f; // rayon ombre projetée (m)
	};

	/// Loader JSON minimal pour `game/data/meshes/overhangs/catalog.json`
	/// (M100.41 — réutilise le pattern hand-rolled de CaveCatalog).
	/// Format identique à `caves/catalog.json` à l'exception de :
	///   - clé racine `overhangs` (au lieu de `caves`),
	///   - `wallAnchorPoint`, `wallNormalDirection`, `coverageRadius`
	///     remplacent `entrancePoint`, `interiorAabbMin`/`Max`.
	class OverhangCatalog
	{
	public:
		bool LoadFromContent(const std::string& contentRoot, std::string& outError);
		bool ParseJson(const std::string& jsonText, std::string& outError);

		const std::vector<OverhangCatalogEntry>& Entries() const { return m_entries; }
		size_t Size() const { return m_entries.size(); }

		const OverhangCatalogEntry* FindById(const std::string& id) const;

	private:
		std::vector<OverhangCatalogEntry> m_entries;
	};
}
