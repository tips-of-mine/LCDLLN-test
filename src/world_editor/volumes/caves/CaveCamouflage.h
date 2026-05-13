#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/shared/math/Math.h"

#include <cstdint>
#include <unordered_map>

namespace engine::editor::world::volumes::caves
{
	/// Deltas splat à appliquer au moment du placement d'une grotte (M100.40).
	/// `outer[chunkCoord][cellIndex] = layer` indique qu'on doit augmenter le
	/// poids de `layer` sur cette cellule. Le poids effectif est calculé via
	/// la formule `weight * 255 * strength * smoothstep(1 - d/radius)`.
	///
	/// Cette structure est volontairement minimaliste pour M100.40 MVP : la
	/// passe d'application est faite par `PlaceCaveCommand` directement avec
	/// le pattern M100.10 (somme=255 préservée), sans introduire un
	/// `SparseSplatDeltas` complet (qui demanderait un snapshot per-cell pour
	/// Undo strict). Le follow-up de la beach splat M100.37 traitera ce
	/// problème en commun.
	struct CaveSplatPatch
	{
		float    worldX        = 0.0f;
		float    worldZ        = 0.0f;
		float    radiusMeters  = 8.0f;
		float    strength      = 0.6f;
		uint8_t  splatLayer    = 5u; // index "rock" par convention layer_palette M100.9
	};

	/// Calcule les cellules de heightmap impactées par le patch (M100.40).
	/// Pour chaque cellule à distance ≤ `radiusMeters` autour de
	/// `(worldX, worldZ)`, génère un poids smoothstep ∈ [0, 1] qui sera
	/// utilisé pour pondérer l'incrément de la couche cible.
	///
	/// Renvoie une map `(chunkCoord) → (cellIndex → weight)`. Vide si patch
	/// dégénéré (rayon ≤ 0 ou force ≤ 0).
	///
	/// Effet de bord : aucun. Pure function.
	engine::editor::world::SparseChunkDeltas ComputeCaveSplatWeights(
		const CaveSplatPatch& patch);
}
