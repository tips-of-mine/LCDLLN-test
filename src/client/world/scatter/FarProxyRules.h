#pragma once

// v1.0.145 — Silhouettes lointaines du bâti (HLOD léger des cellules du monde).
//
// Le bâti (pièces modulaires des maisons, remparts, tours, palais) n'a pas de billboard :
// il disparaît au-delà de world.scatter.cull_large_m (≈ 150 m au préréglage Moyenne), si
// bien qu'une capitale de 600 bâtiments n'est jamais visible en entier. Ce module décide,
// pour chaque pièce posée, quelle SILHOUETTE la remplace au loin : un bloc (murs), un toit
// à deux pans (toitures) ou une flèche conique (toits de tour), dans une teinte proche du
// matériau de la pièce. Les silhouettes sont des meshes unitaires à couleur de sommet
// (game/data/meshes/proxies/, tools/asset_pipeline/proxies/build_far_proxies.py), mis à
// l'échelle de la boîte englobante par la matrice d'instance.
//
// Règle d'affichage (Engine::RecordScatterGeometry) : une silhouette n'est dessinée que si
// la distance horizontale de son centre + son rayon dépasse la distance de culling du bâti
// (une partie au moins des pièces qu'elle remplace n'est plus dessinée), et jusqu'au plan
// lointain. Elle est légèrement rentrée (kFarProxyInset) pour rester cachée derrière les
// vraies pièces dans la bande où les deux coexistent.
//
// Module PUR, header-only, sans Vulkan : testé en CI (far_proxy_rules_tests).

#include <string>
#include <string_view>

namespace engine::world::scatter
{
	/// Forme de silhouette d'une pièce.
	enum class FarProxyShape : unsigned char
	{
		None = 0, ///< pièce sans silhouette (sol, mobilier, ouvertures, pignons…)
		Block,    ///< murs, angles, tours, remparts : bloc
		Gable,    ///< toiture : toit à deux pans (faîtage le long du plus grand côté)
		Spire,    ///< toit de tour : flèche conique
	};

	/// Retrait des silhouettes (facteur appliqué aux dimensions de la boîte englobante).
	inline constexpr float kFarProxyInset = 0.97f;
	/// Taille des cases de la grille du champ des silhouettes (m).
	inline constexpr float kFarProxyGridM = 256.0f;
	/// Plus petite dimension (m) de la boîte d'une pièce pour mériter une silhouette : les
	/// petits éléments (rambardes, poutres, ornements) ne se voient pas de loin.
	inline constexpr float kFarProxyMinExtentM = 1.5f;

	namespace detail
	{
		inline bool Has(std::string_view s, const char* k) { return s.find(k) != std::string_view::npos; }
	}

	/// Forme de silhouette d'une pièce d'après le chemin de son mesh.
	inline FarProxyShape ClassifyFarProxy(std::string_view meshPath)
	{
		using detail::Has;
		// Pièces trop petites, plates ou intérieures : aucun apport de loin.
		for (const char* skip : { "Floor_", "Door", "Window", "Shutter", "Balcony", "Stair", "Prop_", "Table", "Barrel",
			"Crate", "Vase", "Bench", "Chair", "Carpet", "Brazier", "Throne", "Banner", "Lantern", "Sign", "HoleCover",
			"Roof_Front", "Roof_Support", "Roof_FrontSupports", "Overhang_Side", "_BB." })
			if (Has(meshPath, skip)) return FarProxyShape::None;
		if (Has(meshPath, "Roof_Tower") || Has(meshPath, "Spire") || Has(meshPath, "Roof_Cone"))
			return FarProxyShape::Spire;
		if (Has(meshPath, "Roof") || Has(meshPath, "Overhang_Roof"))
			return FarProxyShape::Gable;
		for (const char* solid : { "Wall", "Corner_", "Tower", "Turret", "Rampart", "Palisade", "Keep", "Gate_", "Battlement", "Overhang_" })
			if (Has(meshPath, solid)) return FarProxyShape::Block;
		return FarProxyShape::None;
	}

	/// Famille de teinte d'une pièce (suffixe des meshes de silhouette : Proxy_<forme>_<famille>).
	inline const char* FarProxyFamily(std::string_view meshPath, FarProxyShape shape)
	{
		using detail::Has;
		if (shape == FarProxyShape::Gable || shape == FarProxyShape::Spire)
		{
			if (Has(meshPath, "Wood") || Has(meshPath, "Plank") || Has(meshPath, "Log")) return "Wood";
			if (Has(meshPath, "Shingle") || Has(meshPath, "Slate") || Has(meshPath, "DryStone")) return "Slate";
			if (Has(meshPath, "Thatch") || Has(meshPath, "Straw")) return "Thatch";
			return "Tiles";
		}
		if (Has(meshPath, "Plaster") || Has(meshPath, "WoodGrid")) return "Plaster";
		if (Has(meshPath, "RedBrick")) return "RedBrick";
		if (Has(meshPath, "Brick") || Has(meshPath, "Ochre") || Has(meshPath, "Sandstone")) return "Brick";
		if (Has(meshPath, "Plank") || Has(meshPath, "Wood") || Has(meshPath, "Log") || Has(meshPath, "Palisade")) return "Wood";
		return "Stone";
	}

	/// Chemin (relatif au contenu) du mesh unitaire de silhouette ; vide pour FarProxyShape::None.
	inline std::string FarProxyMeshPath(FarProxyShape shape, const char* family)
	{
		switch (shape)
		{
		case FarProxyShape::Block: return std::string("meshes/proxies/Proxy_Block_") + family + ".gltf";
		case FarProxyShape::Gable: return std::string("meshes/proxies/Proxy_Gable_") + family + ".gltf";
		case FarProxyShape::Spire: return std::string("meshes/proxies/Proxy_Spire_") + family + ".gltf";
		default: return {};
		}
	}

	/// Vrai si une silhouette (centre à \p horizDistM du point de vue, plan XZ, rayon \p radiusM)
	/// doit être dessinée : une partie de ce qu'elle remplace est au-delà de \p buildingCullM
	/// (distance de culling du bâti) et elle reste en deçà de \p farM (plan lointain).
	inline bool FarProxyVisible(float horizDistM, float radiusM, float buildingCullM, float farM)
	{
		return horizDistM + radiusM > buildingCullM && horizDistM - radiusM <= farM;
	}
}
