#pragma once

#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// Encoding spécial pour une cellule sans voisin descendant (sink ou
	/// frontière). Spec M100.36 : valeur 255.
	constexpr uint8_t kSinkDir = 255u;

	/// Tableau des 8 voisins dans l'ordre de tie-break déterministe NE → E →
	/// SE → S → SW → W → NW → N (M100.36 spec §"Tie-break déterministe"). Le
	/// premier voisin testé est NE (dx=+1, dz=+1).
	struct D8Offset { int dx; int dz; };
	constexpr D8Offset kD8Order[8] = {
		{+1, +1},  // 0 NE
		{+1,  0},  // 1 E
		{+1, -1},  // 2 SE
		{ 0, -1},  // 3 S
		{-1, -1},  // 4 SW
		{-1,  0},  // 5 W
		{-1, +1},  // 6 NW
		{ 0, +1},  // 7 N
	};

	/// Pour chaque cellule (x, z) du grid, calcule le voisin (parmi les 8)
	/// vers lequel s'écoulerait l'eau, c.-à-d. celui de **plus grande pente
	/// descendante** : `slope = (h_self - h_neighbour) / distance_meters`.
	/// Une cellule sans voisin strictement descendant émet `kSinkDir`.
	///
	/// Tie-break : si plusieurs voisins ont la même pente max, on garde
	/// le premier dans l'ordre `kD8Order` (NE → ... → N). Ce tie-break est
	/// déterministe (même grid → mêmes directions byte-à-byte).
	///
	/// Les cellules de la frontière (un voisin tombe hors du grid) traitent
	/// le voisin manquant comme inexistant. Si tous les voisins descendants
	/// sont hors-grid, la cellule est un sink.
	///
	/// \param grid Grille de hauteurs source (lecture seule).
	/// \return     Vecteur `width × height` de directions encodées 0..7 ou
	///             `kSinkDir`. Layout row-major Z (`out[z * width + x]`).
	///
	/// Effet de bord : aucun. Pure function, thread-safe.
	std::vector<uint8_t> ComputeD8FlowDirection(const ConsolidatedHeightGrid& grid);
}
