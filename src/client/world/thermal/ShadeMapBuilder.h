#pragma once

// M100.27 — Construction de la shade map à l'export (raycast vertical canopée).
// Pur : prend un échantillonneur de canopée (true = un rayon vertical touche un
// arbre). Délégable à un worker. Testable headless.

#include <cstdint>
#include <functional>

#include "src/client/world/thermal/ShadeMap.h"

namespace engine::world::thermal
{
	/// Renvoie true si un rayon vertical à (worldX,worldZ) touche la canopée
	/// arborée (arbres uniquement, pas l'herbe).
	using CanopySampler = std::function<bool(float worldX, float worldZ)>;

	/// Construit la shade map d'un chunk : pour chaque cellule, 16 échantillons
	/// jittered, shade = min(255, hits*16). Déterministe pour `seed`.
	ShadeMap BuildShadeMap(float chunkOriginX, float chunkOriginZ, float chunkSizeMeters,
	                       const CanopySampler& sampler, uint64_t seed);
}
