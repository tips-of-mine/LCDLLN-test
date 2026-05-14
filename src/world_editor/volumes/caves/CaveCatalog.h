#pragma once

#include "src/shared/math/Math.h"

#include <string>
#include <vector>

namespace engine::editor::world::volumes::caves
{
	/// Entrée du catalogue de grottes (M100.40). Décrit un mesh glTF
	/// disponible pour placement + métadonnées d'ancrage et de volume
	/// intérieur. Le `entrancePoint` est utilisé par le snap au sol :
	/// l'instance est positionnée pour que ce point coïncide avec la
	/// surface terrain.
	struct CaveCatalogEntry
	{
		std::string        id;                  // ex: "cave_small_01"
		std::string        gltfRelativePath;    // ex: "meshes/caves/cave_small_01.gltf"
		std::string        displayName;         // ex: "Petite grotte"
		std::string        thumbnailPath;       // PNG 128×128, content-relative
		engine::math::Vec3 aabbMin;
		engine::math::Vec3 aabbMax;
		engine::math::Vec3 entrancePoint;       // pivot-relatif
		engine::math::Vec3 interiorAabbMin;     // volume jouable pour SurfaceQuery
		engine::math::Vec3 interiorAabbMax;
	};

	/// Loader JSON minimal pour `game/data/meshes/caves/catalog.json`
	/// (M100.40 — hand-rolled JSON parser à la `WorldMapIo`). Le format
	/// attendu :
	/// ```json
	/// {
	///   "caves": [
	///     {
	///       "id": "cave_small_01",
	///       "gltf": "meshes/caves/cave_small_01.gltf",
	///       "displayName": "Petite grotte",
	///       "thumbnail": "meshes/caves/thumbnails/cave_small_01.png",
	///       "aabbMin": [-3, 0, -2],
	///       "aabbMax": [ 3, 4,  2],
	///       "entrancePoint": [0, 0, -2],
	///       "interiorAabbMin": [-2, 0, -1],
	///       "interiorAabbMax": [ 2, 3,  1]
	///     }
	///   ]
	/// }
	/// ```
	///
	/// Tolérant : si le fichier est absent ou malformé, retourne false et
	/// remplit `outError`. Pas d'allocation runtime hors la liste résultat.
	class CaveCatalog
	{
	public:
		/// Charge `catalog.json` depuis `<paths.content>/meshes/caves/`.
		/// Si fichier absent, retourne true avec catalogue vide (mode
		/// "pas encore d'assets").
		bool LoadFromContent(const std::string& contentRoot, std::string& outError);

		/// Parse depuis une string JSON directement (utile pour les tests).
		bool ParseJson(const std::string& jsonText, std::string& outError);

		const std::vector<CaveCatalogEntry>& Entries() const { return m_entries; }
		size_t Size() const { return m_entries.size(); }

		/// Recherche par id. Retourne nullptr si introuvable.
		const CaveCatalogEntry* FindById(const std::string& id) const;

	private:
		std::vector<CaveCatalogEntry> m_entries;
	};
}
