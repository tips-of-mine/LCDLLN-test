#pragma once

// M100.19 — Recette de forêt + échantillonnage terrain pour les générateurs
// procéduraux (Forest / Field). Données pures.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::editor::world
{
	struct ForestRecipeEntry
	{
		std::string assetId;       // référence un asset de la FoliageLibrary (M100.18)
		float weight = 1.0f;       // poids relatif de sélection
		float densityPerM2 = 0.05f;
	};

	struct ForestRecipe
	{
		std::vector<ForestRecipeEntry> entries;
		uint64_t seed = 42;

		float TotalDensity() const
		{
			float d = 0.0f;
			for (const auto& e : entries) d += e.densityPerM2;
			return d;
		}
	};

	/// Échantillon terrain à une position monde (fourni par l'appelant : en jeu
	/// = heightmap/splat réels ; en test = mock).
	struct TerrainSample
	{
		float terrainY = 0.0f;
		float slopeDeg = 0.0f;
		float altMeters = 0.0f;
		int   splatLayer = 0;
	};

	using TerrainSampler = std::function<TerrainSample(float worldX, float worldZ)>;
}
