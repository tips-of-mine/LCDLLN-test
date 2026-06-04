#pragma once

// M100.28 — Requêtes pures sur les zones de gameplay (point-in-zone, blend de
// transition météo au bord). Indépendant du rendu : testable headless.

#include <vector>

#include "src/client/world/zones/Zones.h"
#include "src/shared/math/Math.h"

namespace engine::world::zones
{
	/// True si (x,z) est dans le polygone de la zone (ray casting).
	bool PointInZone(const GameplayZone& zone, float x, float z);

	/// Distance (m) du point au bord le plus proche du polygone, SIGNÉE :
	/// positive si dedans, négative si dehors.
	float SignedDistanceToPolygon(const std::vector<engine::math::Vec3>& polygon, float x, float z);

	/// Facteur de blend météo d'une WeatherOverride : 0 dehors / au bord,
	/// 1 au-delà de `transitionMarginMeters` à l'intérieur, lerp linéaire entre.
	float WeatherOverrideBlend(const GameplayZone& zone, float x, float z);
}
