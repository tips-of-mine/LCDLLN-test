#pragma once

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// Grille de hauteurs en RAM assemblant N×M cellules contiguës d'un sous-
	/// ensemble de chunks (M100.36). Pure data, indépendante de
	/// `TerrainDocument` : tests et algorithmes (D8 flow direction, flow
	/// accumulation, érosion future M100.38/39) opèrent dessus.
	///
	/// Layout row-major en Z : `heights[z * width + x]`.
	///
	/// Conventions :
	///   - `width` / `height` : nombre de cellules le long de X / Z.
	///   - `cellSizeMeters`   : pas physique d'une cellule (1.0 m pour M100.5).
	///   - `originCellX` /Z   : coord cellule globale du coin (0, 0) du grid
	///                          (i.e. dans le repère "monde / cellSize").
	///
	/// Effet de bord : aucun (struct passive). M100.36 introduit le contrat ;
	/// M100.38 (hydraulic erosion) et M100.39 (thermal/wind erosion) le
	/// consomment sans refactor.
	struct ConsolidatedHeightGrid
	{
		int width  = 0;
		int height = 0;
		float cellSizeMeters = 1.0f;
		int originCellX = 0;
		int originCellZ = 0;
		std::vector<float> heights;

		/// Construit une grille plate `w × h` initialisée à `y`. Utile pour
		/// les tests et le bootstrap d'une simulation sur zone vide.
		static ConsolidatedHeightGrid MakeFlat(int w, int h, float y);

		/// Indexe une cellule. Précondition : `0 <= x < width`, `0 <= z < height`.
		float Get(int x, int z) const { return heights[static_cast<size_t>(z) * width + x]; }
		void  Set(int x, int z, float v) { heights[static_cast<size_t>(z) * width + x] = v; }
	};
}
