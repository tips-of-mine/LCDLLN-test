#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <cstdint>

namespace engine::editor::world::erosion
{
	/// Résultat de la simulation d'une goutte unique (M100.38). Sert au
	/// orchestrateur pour cumuler les stats globales.
	struct DropletKernelResult
	{
		uint32_t stepsTaken      = 0;
		uint32_t cellsEroded     = 0;
		uint32_t cellsDeposited  = 0;
	};

	/// Simule une goutte unique partant de `(startCellX, startCellZ)`,
	/// applique les deltas dans `outDeltas`. Lit uniquement la grid pristine
	/// (pas de feedback entre gouttes pour garantir le déterminisme et
	/// l'invariance parallèle vs single-thread).
	///
	/// Algorithme (cf. spec §"Partie C") :
	///   - boucle ≤ `maxLifetimeSteps`,
	///   - velocity = velocity * (1 - inertia) + gradient * gravity,
	///   - capacité = max(-Δh, minSlope) * |velocity| * water * sedimentCapacity,
	///   - érosion si sediment < capacité, déposition sinon,
	///   - terminaison sur sea level / hors-grid / water trop évaporée.
	///
	/// \param grid             Grid pristine (lecture seule).
	/// \param params           Paramètres de simulation (constants).
	/// \param seaLevelMeters   Sea level pour la condition de stop.
	/// \param startCellX       Position initiale X (cellule fractionnaire).
	/// \param startCellZ       Position initiale Z (cellule fractionnaire).
	/// \param outDeltas        Buffer cumulatif (additif).
	/// \return                 Stats de la goutte (steps + cellules touchées).
	///
	/// Effet de bord : modifie `outDeltas` uniquement.
	DropletKernelResult RunSingleDroplet(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		const HydraulicSimulationParams& params,
		float seaLevelMeters,
		float startCellX, float startCellZ,
		engine::editor::world::SparseChunkDeltas& outDeltas);
}
