#pragma once

#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <utility>
#include <vector>

namespace engine::editor::world
{
	/// Segment 2D en world space XZ (mètres). Représente une portion de
	/// l'iso-courbe `altitude == seaLevelMeters` extraite par marching squares
	/// (M100.37).
	struct CoastlineSegment
	{
		float ax;
		float az;
		float bx;
		float bz;
	};

	/// Extrait l'iso-courbe `altitude == seaLevelMeters` du grid consolidé
	/// par algorithme de marching squares 2D.
	///
	/// Pour chaque carré 2×2 cellules adjacentes, classifie les 4 coins
	/// (1 = au-dessus du sea level, 0 sinon), construit un code 4-bits puis
	/// pose 0, 1 ou 2 segments interpolés linéairement entre les arêtes
	/// traversées par l'iso-altitude. Les segments sont rendus en world
	/// space en utilisant `grid.cellSizeMeters` et `grid.originCellX/Z`.
	///
	/// Cas ambigus (codes 5 et 10, "selle de cheval") : résolus par la
	/// moyenne des 4 coins ; deux segments distincts si la moyenne se
	/// trouve du côté terre, sinon une croix.
	///
	/// \param grid             Grille de hauteurs (lecture seule).
	/// \param seaLevelMeters   Iso-altitude à tracer.
	/// \return                 Liste de segments en world XZ. Vide si grid
	///                         entièrement terre ou entièrement mer.
	///
	/// Effet de bord : aucun. Pure function, thread-safe.
	std::vector<CoastlineSegment> ExtractCoastlineSegments(
		const ConsolidatedHeightGrid& grid, float seaLevelMeters);
}
