#pragma once

#include "src/world_editor/water/ConsolidatedHeightGrid.h"
#include "src/world_editor/water/RiverNetworkResult.h"
#include "src/world_editor/water/WatershedSimulationParams.h"

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;

	/// Exécute la simulation watershed sur une grille de hauteurs déjà
	/// assemblée (M100.36 — version pure-function pour les tests). Effet de
	/// bord : aucun.
	///
	/// \param grid             Grille consolidée (lecture seule).
	/// \param seaLevelMeters   Niveau de mer global (typiquement lu depuis
	///                         `WaterDocument::GetOcean().seaLevelMeters`).
	/// \param params           Paramètres de simulation.
	/// \return                 Résultat complet (rivers + lakes + carving deltas).
	///
	/// Thread-safe (pas d'état global mutable).
	RiverNetworkResult RunWatershedOnGrid(
		const ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const WatershedSimulationParams& params);

	/// Variante éditeur : assemble la grille depuis `terrain` autour du
	/// bounding box des sources de `params`, puis appelle `RunWatershedOnGrid`.
	/// Lit `seaLevelMeters` depuis `water.GetOcean().seaLevelMeters` (jamais
	/// depuis `params`).
	///
	/// \param terrain  Document terrain (lecture seule logique ; `EnsureLoaded`
	///                 charge les chunks manquants en bloquant le main thread).
	/// \param water    Document eau (lecture seule, source du sea level).
	/// \param cfg      Config pour `EnsureLoaded` (`paths.content`).
	/// \param params   Paramètres de simulation (springs + flags).
	RiverNetworkResult RunWatershedSimulation(
		TerrainDocument& terrain,
		const WaterDocument& water,
		const engine::core::Config& cfg,
		const WatershedSimulationParams& params);
}
