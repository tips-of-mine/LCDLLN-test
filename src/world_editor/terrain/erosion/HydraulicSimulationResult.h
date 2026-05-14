#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas

#include <cstdint>

namespace engine::editor::world::erosion
{
	/// Résultat immuable d'une simulation d'érosion hydraulique (M100.38).
	/// `deltas` contient les modifications cumulées à appliquer à la
	/// heightmap via `HydraulicErosionCommand` (réutilise le typedef
	/// `SparseChunkDeltas` introduit par M100.35).
	struct HydraulicSimulationResult
	{
		SparseChunkDeltas deltas;

		// Statistiques pour l'UI résultat.
		uint32_t dropletsSimulated = 0;
		uint64_t totalSteps        = 0;
		uint32_t cellsEroded       = 0;
		uint32_t cellsDeposited    = 0;
		float    minDelta          = 0.0f;
		float    maxDelta          = 0.0f;
		double   wallTimeMillis    = 0.0;
	};
}
