#pragma once

// Auberge éditable (T1) — Catalogue d'assets : scan disque des meshes props
// pour alimenter l'AssetBrowserPanel. Logique pure (std::filesystem), testable
// headless ; la couche ImGui consomme ScanPropAssets() en lecture seule.

#include <string>
#include <vector>

namespace engine::editor::world::assets
{
	/// Catégorie sémantique d'un asset prop (dérivée du préfixe de son nom de fichier).
	/// Chaque valeur correspond à un groupe affiché dans l'AssetBrowserPanel.
	enum class AssetCategory
	{
		Wall, Door, Window, Roof, Floor, Corner, Overhang, Balcony, Stairs,
		Furniture, Lighting, Container, Decoration, Unknown
	};

	/// Un asset découvert sur le disque.
	struct AssetEntry
	{
		std::string fileName;      ///< Nom court, ex. "Wall_Plaster_Straight.gltf"
		std::string relativePath;  ///< Chemin relatif à la racine data, ex. "meshes/props/Wall_Plaster_Straight.gltf"
		AssetCategory category = AssetCategory::Unknown; ///< Catégorie déduite du préfixe
	};

	/// Déduit la catégorie d'un asset à partir du préfixe de son nom de fichier
	/// (partie avant le premier '_', ou nom entier s'il n'y a pas de '_').
	/// \param fileName  Nom de fichier court (avec extension), ex. "Door_2_Round.gltf".
	/// \return AssetCategory::Unknown si le préfixe n'est pas reconnu.
	AssetCategory CategorizeAsset(const std::string& fileName);

	/// Retourne le libellé localisé (français) d'une catégorie, affiché
	/// comme en-tête de groupe dans l'AssetBrowserPanel.
	/// \param c  La catégorie à libeller.
	/// \return Chaîne statique (pas d'allocation), toujours valide.
	const char* CategoryLabel(AssetCategory c);

	/// Scanne récursivement \p absoluteDir à la recherche de fichiers .gltf,
	/// construit un AssetEntry par fichier (catégorie + chemin relatif), puis
	/// trie le résultat par (catégorie, nom) pour un affichage ordonné.
	/// \param absoluteDir    Chemin absolu du répertoire à scanner (ex. "C:/data/meshes/props").
	///                       Si le répertoire est absent ou inaccessible, retourne un vecteur vide.
	/// \param relativePrefix Préfixe ajouté devant le nom de fichier pour construire relativePath
	///                       (ex. "meshes/props/").
	/// \return Vecteur trié d'AssetEntry ; vide si le répertoire n'existe pas.
	/// \note Fonction pure (pas d'état global modifié). Appelable depuis n'importe quel thread.
	std::vector<AssetEntry> ScanPropAssets(const std::string& absoluteDir,
		const std::string& relativePrefix);
}
