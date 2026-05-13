#pragma once

#include "src/world_editor/water/ConsolidatedHeightGrid.h"
#include "src/world_editor/water/CoastlineSegmentExtractor.h"

#include <cstdint>
#include <span>

namespace engine::editor::world
{
	/// Statistiques live affichées par l'UI Coastline (M100.37). Toutes
	/// recalculées par des fonctions O(N) sur le grid consolidé.
	struct CoastlineStats
	{
		/// Cellules dont l'altitude est strictement supérieure au sea level.
		uint32_t landCells = 0;
		/// Cellules dont l'altitude est inférieure ou égale au sea level.
		uint32_t oceanCells = 0;
		/// Longueur totale des segments marching squares (mètres monde).
		float    coastlineLengthMeters = 0.0f;
		/// Cellules dans la bande verticale [sea - mer, sea + terre]
		/// utilisée par la "plage automatique".
		uint32_t beachBandCells = 0;
	};

	/// Calcule terre / océan / longueur côte / bande plage en une passe O(N).
	///
	/// \param grid                Grid consolidé (lecture seule).
	/// \param seaLevelMeters      Sea level pour partitionner.
	/// \param beachLandBandMeters Largeur verticale côté terre pour la plage.
	/// \param beachSeaBandMeters  Largeur verticale côté mer pour la plage.
	/// \param segments            Segments marching squares (somme des longueurs).
	///
	/// Effet de bord : aucun. Pure function.
	CoastlineStats ComputeCoastlineStats(
		const ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		float beachLandBandMeters,
		float beachSeaBandMeters,
		std::span<const CoastlineSegment> segments);
}
