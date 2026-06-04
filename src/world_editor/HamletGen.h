#pragma once

// M100.31 — Génération d'hameau (PURE, déterministe). Candidats Poisson-disk
// (M100.18), filtrage pente, snap route optionnel, sélection mesh pondérée.
// Produit des PropInstance (M100.17). L'outil/commande posent via PlacePropsCommand.

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "src/client/world/instances/PropInstances.h"
#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	struct HamletRecipe
	{
		std::vector<std::pair<std::string, float>> houseMeshes; // mesh, weight (du kit)
		int houseCount = 12;
		float minSpacing = 8.0f;
		float maxSlopeDeg = 15.0f;
		bool snapToRoad = false;
		float roadSnapRangeMeters = 30.0f;
		float roadOffsetMeters = 4.0f;
		uint64_t seed = 42;
	};

	struct HamletTerrainSample
	{
		float slopeDeg = 0.0f;
		float terrainY = 0.0f;
	};

	using HamletSampler = std::function<HamletTerrainSample(float worldX, float worldZ)>;

	/// Génère les maisons d'un hameau dans `polygon`. `road` (polyline, vide si
	/// aucune) sert au snap. `nextInstanceId` est avancé pour chaque maison.
	std::vector<engine::world::instances::PropInstance> GenerateHamlet(
		const std::vector<engine::math::Vec3>& polygon, const HamletRecipe& recipe,
		const HamletSampler& sampler, const std::vector<engine::math::Vec3>& road,
		uint32_t& nextInstanceId);
}
