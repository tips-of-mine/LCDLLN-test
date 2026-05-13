#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

namespace engine::editor::world
{
	/// Lissage Gaussien 3×3 read-stable des cellules dans la bande verticale
	/// `[seaLevel - bandMeters, seaLevel + bandMeters]` (M100.37). Le kernel
	/// lit la heightmap pré-edit (passée en `pristineGrid`), produit un
	/// delta `(h_smoothed - h_original) * force`.
	///
	/// Multi-chunks : les deltas sont émis dans `SparseChunkDeltas` indexés
	/// par `GlobalChunkCoord` + cellIndex linéaire. Couture inter-chunks
	/// préservée par construction (rasterisation en espace cellule monde).
	///
	/// \param pristineGrid     Grid consolidé pré-edit (lecture seule).
	/// \param seaLevelMeters   Sea level du tick.
	/// \param bandMeters       Demi-largeur verticale de la bande
	///                         (cellules hors bande : delta = 0 strict).
	/// \param force            Multiplicateur [0..1] du delta lissage.
	/// \return                 Deltas sparse à appliquer via `CoastlineCommand`.
	///
	/// Effet de bord : aucun. Pure function, thread-safe.
	SparseChunkDeltas ComputeCoastlineSmoothingDeltas(
		const ConsolidatedHeightGrid& pristineGrid,
		float seaLevelMeters,
		float bandMeters,
		float force);
}
