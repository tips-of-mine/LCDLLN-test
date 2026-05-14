#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/terrain/erosion/ThermalSimulationParams.h"
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <cstdint>

namespace engine::editor::world::erosion
{
	struct ThermalSimulationResult
	{
		engine::editor::world::SparseChunkDeltas deltas;
		uint32_t passesExecuted = 0;
		bool     converged      = false;
		uint32_t cellsAffected  = 0;
		float    totalTransferredMeters = 0.0f;
		double   wallTimeMillis = 0.0;
	};

	/// Exécute la simulation thermique sur une copie locale de `grid`. Mute
	/// `grid` passe par passe (chaque passe lit le grid post-passe précédente).
	/// Les deltas cumulés sont retournés à committer via ICommand.
	///
	/// Effet de bord : `grid.heights` est modifié. Pas thread-safe (passes
	/// intrinsèquement séquentielles).
	ThermalSimulationResult RunThermalSimulation(
		engine::editor::world::ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const ThermalSimulationParams& params);
}
