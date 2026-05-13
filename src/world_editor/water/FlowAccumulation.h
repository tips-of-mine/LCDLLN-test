#pragma once

#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// À partir d'une grille de hauteurs + des directions D8 calculées par
	/// `ComputeD8FlowDirection`, calcule la quantité de cellules amont qui
	/// s'écoulent vers chaque cellule (M100.36).
	///
	/// Algorithme : tri topologique des cellules par altitude décroissante
	/// puis propagation cumulative — chaque cellule i propage `flowAcc[i]`
	/// à sa cellule aval (selon D8) en additionnant. Initialisation à 1
	/// (chaque cellule contribue elle-même).
	///
	/// Le résultat est en "unités cellules" (le nombre de cellules dont le
	/// flux converge vers la cellule courante).
	///
	/// \param grid       Grille source (sert au tri par altitude).
	/// \param flowDirs   Directions D8 (`width × height`, layout row-major).
	/// \return           `flowAcc` (uint32) de même taille que flowDirs.
	///
	/// Effet de bord : aucun. Pure function, thread-safe. Coût : O(N log N)
	/// pour le tri + O(N) pour la propagation, où N = width × height.
	std::vector<uint32_t> ComputeFlowAccumulation(
		const ConsolidatedHeightGrid& grid,
		const std::vector<uint8_t>& flowDirs);
}
