#pragma once

// M100.50 — Fonctions d'auto-génération ($auto_xxx du template). Déterministes
// (std::mt19937_64 seedé) : même seed → même sortie. Pures, testables.

#include <cstdint>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::editor::world::wizard
{
	/// Génère une polyline de chaîne montagneuse adaptée au relief.
	///   plains    : 2 points, quasi plat.
	///   hills     : 3 points, sinuosité légère, hauteur modérée.
	///   mountains : 5 points, plus dramatique.
	///   escarped  : 5 points, amplitude maximale.
	/// Déterministe pour (relief, seed) : un même couple produit la même polyline.
	std::vector<engine::math::Vec3> GenerateMountainPolyline(const std::string& relief, uint32_t seed);
}
