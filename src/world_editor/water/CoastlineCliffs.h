#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

namespace engine::editor::world
{
	/// Passe "falaises côtières" (M100.37). Pour chaque cellule dans la bande
	/// verticale `[sea - threshold, sea + threshold]` :
	///   - calcule la pente locale `|grad(heights)|` (différences finies),
	///   - si la pente excède `slopeThresholdDeg`, élève la cellule côté
	///     terre (`+cliffLandSideMeters * weight`) ou abaisse côté mer
	///     (`-cliffSeaSideMeters * weight`),
	///   - `weight = smoothstep(seaLevel ± thresholdMeters, ...)`.
	///
	/// Multi-chunks : émet dans `SparseChunkDeltas`. Couture inter-chunks
	/// préservée par construction (rasterisation en cellules monde).
	///
	/// Effet de bord : aucun. Pure function, thread-safe.
	SparseChunkDeltas ComputeCoastlineCliffsDeltas(
		const ConsolidatedHeightGrid& pristineGrid,
		float seaLevelMeters,
		float thresholdMeters,
		float slopeThresholdDeg,
		float cliffLandSideMeters,
		float cliffSeaSideMeters);
}
