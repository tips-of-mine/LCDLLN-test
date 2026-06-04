#pragma once

// M100.18 — Échantillonneur Poisson-disk 2D (plan XZ). Pur et déterministe.

#include <cstdint>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::foliage
{
	/// Génère des points dans le rectangle [0,width] × [0,height] (plan XZ, y=0)
	/// tels qu'aucune paire ne soit à distance < `minRadius` (algorithme de
	/// Bridson). Déterministe pour un `seed` donné.
	std::vector<engine::math::Vec3> SamplePoissonDisk(float width, float height,
	                                                  float minRadius, uint64_t seed);
}
