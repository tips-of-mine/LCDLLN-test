#pragma once

// Bibliothèque de TYPES de bâtiments (auberges, maisons, tours…). Chaque type
// est un fichier JSON dans `game/data/buildings/templates/<type>.json` et
// contient plusieurs VARIANTES (créations). Une variante = une grappe de pièces
// (meshes du kit + transform local). La carte ne stocke que des RÉFÉRENCES
// (type + variante + transform monde, cf. BuildingPlacement dans Buildings.h) ;
// le jeu résout ces références contre la bibliothèque pour afficher.
//
// Ce header ne porte que les STRUCTS (data model). Le chargement/sauvegarde
// JSON est dans BuildingTemplateLibrary.{h,cpp} (utilise le parseur Config).

#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::instances
{
	/// Une pièce d'une variante de bâtiment, en espace LOCAL (relatif à
	/// l'origine du bâtiment placé).
	struct BuildingPart
	{
		std::string        gltfRelativePath;            // ex: "meshes/props/Wall_Plaster_Straight.gltf"
		engine::math::Vec3 localPosition{ 0.0f, 0.0f, 0.0f }; // offset local (m)
		engine::math::Vec3 localEulerDeg{ 0.0f, 0.0f, 0.0f }; // rotation XYZ locale (degrés)
		float              localScale = 1.0f;           // échelle uniforme locale
		bool               solid = true;                // pose un cylindre de collision
		float              collisionRadius = 0.0f;      // 0 => rayon auto (empreinte XZ)
	};

	/// Une variante (création) d'un type de bâtiment : un id stable + un nom
	/// lisible + la liste des pièces qui la composent.
	struct BuildingVariant
	{
		std::string               id;          // stable, référencé par la carte (ex: "auberge_terrasse")
		std::string               displayName; // libre (ex: "Auberge — terrasse")
		std::vector<BuildingPart> parts;
	};

	/// Un type de bâtiment = un fichier de la bibliothèque. Porte le `type`
	/// (clé de référence, ex: "tavern") + un nom lisible + ses variantes.
	struct BuildingTemplate
	{
		std::string                  type;        // clé de référence (nom de fichier sans extension)
		std::string                  displayName; // libre (ex: "Taverne / Auberge")
		std::vector<BuildingVariant> variants;

		/// Recherche une variante par id. nullptr si absente.
		const BuildingVariant* FindVariant(const std::string& variantId) const
		{
			for (const auto& v : variants)
				if (v.id == variantId) return &v;
			return nullptr;
		}
	};
}
