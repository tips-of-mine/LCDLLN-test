#pragma once

#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

namespace engine::editor::world::erosion
{
	/// Échantillonnage bilinéaire d'une heightmap + gradient local (M100.38).
	/// `posCellX`, `posCellZ` sont en coords cellule du grid (peuvent être
	/// fractionnaires). Le gradient est calculé par interpolation bilinéaire
	/// des dérivées finies aux 4 coins.
	struct HeightAndGradient
	{
		float height;
		float gradientX;   // ∂h/∂x dans le repère cellule
		float gradientZ;   // ∂h/∂z dans le repère cellule
	};

	/// Échantillonne hauteur + gradient à `(posCellX, posCellZ)` (coords
	/// cellule, fractionnaires acceptées). Si la position sort du grid,
	/// retourne `{0, 0, 0}` et le caller doit terminer la goutte.
	HeightAndGradient SampleHeightAndGradient(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float posCellX, float posCellZ);

	/// Distribue un delta `value` (en mètres) sur les 4 cellules entourant
	/// `(posCellX, posCellZ)` selon les poids bilinéaires (M100.38).
	/// Écrit dans `outDeltas` indexé par `GlobalChunkCoord` + cellIndex.
	///
	/// Multi-chunks : la cellule pile sur la frontière `cell % (kRes-1) == 0`
	/// reçoit le même delta vue des deux chunks adjacents (couture préservée
	/// par construction — la position monde est partagée).
	///
	/// \param grid       Grid consolidé (lecture seule, pour les origins).
	/// \param posCellX   Position cellule X (fractionnaire).
	/// \param posCellZ   Position cellule Z (fractionnaire).
	/// \param value      Delta en mètres (positif = déposition, négatif = érosion).
	/// \param outDeltas  Buffer cumulatif (additif).
	///
	/// Effet de bord : modifie `outDeltas` uniquement. Hors-grid : no-op.
	void DistributeBilinearDelta(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float posCellX, float posCellZ,
		float value,
		engine::editor::world::SparseChunkDeltas& outDeltas);
}
