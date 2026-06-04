#pragma once

// M100.19 — Générateurs procéduraux Forest & Field (PURS, déterministes).
// Émettent des FoliageInstance (M100.18) ; le tagging splat et l'UI sont gérés
// ailleurs (différés). Indépendants du rendu : testables headless.

#include <vector>

#include "src/client/world/foliage/FoliageInstances.h"
#include "src/client/world/foliage/FoliageLibrary.h"
#include "src/shared/math/Math.h"
#include "src/world_editor/ForestRecipe.h"

namespace engine::editor::world
{
	/// Génère une forêt dans un polygone fermé (sommets en XZ, y ignoré).
	/// Densité totale = somme des densités de la recette ; sélection d'asset par
	/// poids cumulé ; filtrage par règles per-asset (M100.18) via `sampler`.
	/// Déterministe pour `recipe.seed`.
	std::vector<engine::world::foliage::FoliageInstance> GenerateForest(
		const std::vector<engine::math::Vec3>& polygon,
		const ForestRecipe& recipe,
		const engine::world::foliage::FoliageLibrary& library,
		const TerrainSampler& sampler);

	struct FieldParams
	{
		engine::math::Vec3 corner{ 0.0f, 0.0f, 0.0f }; // coin (origine de la grille)
		float width = 10.0f;       // extension X locale (m)
		float depth = 10.0f;       // extension Z locale (m)
		float spacing = 0.6f;      // pas régulier (m)
		float rotationDeg = 0.0f;  // rotation de la grille autour du coin
		uint64_t seed = 1;
	};

	/// Génère un champ : grille régulière (rotation appliquée), une instance par
	/// cellule, scale légèrement aléatoire [0.95,1.05], position SANS jitter
	/// (alignement régulier). Déterministe.
	std::vector<engine::world::foliage::FoliageInstance> GenerateField(
		const FieldParams& params, uint32_t assetIdHash, const TerrainSampler& sampler);
}
