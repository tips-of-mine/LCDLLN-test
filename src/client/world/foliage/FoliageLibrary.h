#pragma once

// M100.18 — Bibliothèque végétale : catalogue (library.json) + règles de
// placement. Le parsing JSON et le filtrage par règles sont purs et testables.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::world::foliage
{
	/// Règles de placement d'un asset végétal.
	struct FoliageRules
	{
		float slopeMaxDeg = 90.0f;
		float altMin = -100000.0f;
		float altMax = 100000.0f;
		std::vector<int> splatLayers; // vide = toutes couches acceptées
	};

	struct FoliageAsset
	{
		std::string id;
		std::string category;
		std::string mesh;
		bool billboard = false;
		FoliageRules rules;
	};

	struct FoliageCategory
	{
		std::string id;
		std::string label;
	};

	struct FoliageLibrary
	{
		uint32_t version = 0;
		std::vector<FoliageCategory> categories;
		std::vector<FoliageAsset> assets;

		const FoliageAsset* FindAsset(const std::string& id) const
		{
			for (const auto& a : assets) if (a.id == id) return &a;
			return nullptr;
		}
	};

	/// True si une cellule de pente `slopeDeg`, altitude `altMeters` et couche
	/// splat dominante `splatLayer` satisfait les règles de l'asset.
	bool PassesRules(const FoliageRules& rules, float slopeDeg, float altMeters, int splatLayer);

	/// Parse le contenu JSON de `library.json`. Tolérant aux clés inconnues ;
	/// renseigne `err` et renvoie false en cas d'échec structurel.
	bool ParseFoliageLibraryJson(const std::string& jsonText, FoliageLibrary& out, std::string& err);
}
