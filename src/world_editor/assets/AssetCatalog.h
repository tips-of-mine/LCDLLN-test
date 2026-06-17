#pragma once

#include <string>
#include <vector>

namespace engine::editor::world::assets
{
	/// Une entrée du catalogue d'assets de l'éditeur : un mesh glTF du kit
	/// `game/data/meshes/props/` utilisable comme pièce de bâtiment ou prop.
	/// La catégorie est portée EN CLAIR (dérivée par le générateur depuis le
	/// préfixe de nom) pour éviter une heuristique côté C++.
	struct AssetCatalogEntry
	{
		std::string id;               // ex: "Wall_Plaster_Straight"
		std::string category;         // ex: "Wall", "Door", "Roof", "Furniture"…
		std::string gltfRelativePath; // ex: "meshes/props/Wall_Plaster_Straight.gltf"
		std::string displayName;      // libre, par défaut = id
		std::string thumbnailPath;    // optionnel (content-relative), peut être vide
	};

	/// Catalogue d'assets de l'éditeur, alimenté par
	/// `game/data/meshes/props/catalog.json` (généré par
	/// `tools/asset_pipeline/generate_props_catalog.py`).
	///
	/// Format JSON attendu (compatible parseur Config — `count` + clés
	/// indexées, comme `scenery.json`) :
	/// ```json
	/// {
	///   "assets": {
	///     "count": 2,
	///     "0": { "id": "Wall_Plaster_Straight", "category": "Wall",
	///            "gltf": "meshes/props/Wall_Plaster_Straight.gltf",
	///            "displayName": "Mur plâtre droit", "thumbnail": "" },
	///     "1": { "id": "Door_1_Flat", "category": "Door",
	///            "gltf": "meshes/props/Door_1_Flat.gltf" }
	///   }
	/// }
	/// ```
	///
	/// Tolérant : fichier absent → catalogue vide + true (mode « pas encore de
	/// catalogue »). JSON invalide → false + `outError`.
	class AssetCatalog
	{
	public:
		/// Charge `meshes/props/catalog.json` depuis \p contentRoot. Fichier
		/// absent : true avec catalogue vide.
		bool LoadFromContent(const std::string& contentRoot, std::string& outError);

		/// Parse depuis une string JSON (tests / chargement custom).
		bool ParseJson(const std::string& jsonText, std::string& outError);

		const std::vector<AssetCatalogEntry>& Entries() const { return m_entries; }
		size_t Size() const { return m_entries.size(); }

		/// Recherche par id. nullptr si introuvable.
		const AssetCatalogEntry* FindById(const std::string& id) const;

		/// Pointeurs vers les entrées d'une catégorie donnée (ordre du catalogue).
		std::vector<const AssetCatalogEntry*> ByCategory(const std::string& category) const;

		/// Liste ordonnée et dédupliquée des catégories présentes (ordre de
		/// première apparition dans le catalogue).
		std::vector<std::string> Categories() const;

	private:
		std::vector<AssetCatalogEntry> m_entries;
	};
}
