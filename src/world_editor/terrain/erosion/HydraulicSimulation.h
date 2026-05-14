#pragma once

#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationResult.h"
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;
}

namespace engine::editor::world::erosion
{
	/// Exécute la simulation d'érosion sur une grille pré-assemblée (M100.38
	/// — version pure pour les tests). Pas d'effet de bord sur les documents.
	///
	/// \param grid             Grid pristine (lecture seule).
	/// \param seaLevelMeters   Sea level lu depuis `WaterDocument::GetOcean()`
	///                         (M100.37) par l'appelant orchestrateur.
	/// \param params           Paramètres de simulation.
	/// \return                 Résultat (deltas + stats).
	///
	/// Thread-safe (pas d'état global). Coût ~3 s pour 100k gouttes en
	/// single-thread sur un grid 5121².
	HydraulicSimulationResult RunHydraulicOnGrid(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const HydraulicSimulationParams& params);

	/// Variante éditeur : assemble la grid à partir des chunks chargés via
	/// `TerrainDocument::EnsureLoaded`, lit le sea level depuis
	/// `WaterDocument::GetOcean()`, puis appelle `RunHydraulicOnGrid`.
	HydraulicSimulationResult RunHydraulicSimulation(
		engine::editor::world::TerrainDocument& terrain,
		const engine::editor::world::WaterDocument& water,
		const engine::core::Config& cfg,
		const HydraulicSimulationParams& params);
}
