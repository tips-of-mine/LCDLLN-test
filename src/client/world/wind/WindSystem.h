#pragma once

// M100.20 — Évaluation du vent effectif (global ± zones locales) + miroir C++
// de la logique de sway du shader foliage.vert (pour test headless). Pur.

#include <vector>

#include "src/client/world/wind/WindParams.h"
#include "src/client/world/wind/WindZones.h"

namespace engine::world::wind
{
	/// Poids de sway selon la hauteur normalisée (smoothstep 0..1) : racines
	/// fixes (0), sommet ondulant (1).
	float SwayHeightWeight(float heightNormalized);

	/// Magnitude du déplacement de sway à une position monde / hauteur — miroir
	/// exact de `foliage.vert` (test : proportionnalité à la hauteur).
	float ComputeSwayMagnitude(const WindParamsCpu& w, float worldX, float worldZ, float heightNormalized);

	/// Paramètres effectifs à la position caméra : si elle est dans une zone
	/// locale, ses overrides (direction/force/turbulence) remplacent le global.
	/// La première zone contenant le point gagne (ordre stable).
	WindParamsCpu EvaluateWind(const WindParamsCpu& global, const std::vector<WindZone>& zones,
	                           float camX, float camZ);
}
