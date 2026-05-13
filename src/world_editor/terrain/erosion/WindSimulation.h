#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/terrain/erosion/WindSimulationParams.h"
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <cstdint>

namespace engine::editor::world::erosion
{
	struct WindSimulationResult
	{
		engine::editor::world::SparseChunkDeltas deltas;
		uint32_t particlesSimulated = 0;
		uint64_t totalSteps         = 0;
		uint32_t cellsEroded        = 0;
		uint32_t cellsDeposited     = 0;
		float    minDelta           = 0.0f;
		float    maxDelta           = 0.0f;
		double   wallTimeMillis     = 0.0;
	};

	/// Exécute la simulation éolienne sur `grid` (lecture seule). Les
	/// particules avancent dans la direction du vent dominant, érodent les
	/// faces exposées (différence d'altitude positive vs amont) et déposent
	/// sur les faces abritées.
	///
	/// Effet de bord : aucun sur `grid`. Pure function, thread-safe.
	WindSimulationResult RunWindSimulation(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const WindSimulationParams& params);
}
