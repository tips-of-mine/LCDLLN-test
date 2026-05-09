#pragma once

#include "src/client/world/terrain/TerrainChunk.h"

#include <cstdint>

namespace engine::editor::world
{
	/// Calcule la pente (en degrés) à la cellule `(x, z)` du chunk (M100.10).
	/// Utilise les voisins immédiats pour estimer le gradient bilinéaire :
	/// `tan(slope) = sqrt((dh/dx)² + (dh/dz)²)`, retourne `atan(tan(slope))`
	/// converti en degrés. Aux bords du chunk, le voisin manquant est dupliqué
	/// (même convention que `TerrainMeshBuilder::ComputeNormalAndUv`).
	///
	/// \param chunk Chunk source (lecture seule).
	/// \param x     Index colonne dans `[0, chunk.resolutionX-1]`.
	/// \param z     Index ligne dans `[0, chunk.resolutionZ-1]`.
	/// \return Pente en degrés dans `[0, 90]`. Si `(x, z)` est hors résolution,
	///         retourne 0.0f (pas d'erreur, pas d'assertion).
	float ComputeSlopeDeg(const engine::world::terrain::TerrainChunk& chunk,
		uint32_t x, uint32_t z);

	/// Vrai si la cellule `(x, z)` du chunk satisfait simultanément :
	///   - pente dans `[slopeMinDeg, slopeMaxDeg]`
	///   - altitude (= `chunk.heights[z*resX+x]`) dans `[altMin, altMax]`
	/// (M100.10). Utilisée par le mode auto-rules du `SplatPaintTool` pour
	/// décider si une cellule reçoit la couche active.
	///
	/// \param chunk        Chunk source (lecture seule).
	/// \param x, z         Coordonnées chunk-locales (cf. `ComputeSlopeDeg`).
	/// \param slopeMinDeg  Pente minimale (incluse).
	/// \param slopeMaxDeg  Pente maximale (incluse).
	/// \param altMin       Altitude minimale en mètres (incluse).
	/// \param altMax       Altitude maximale en mètres (incluse).
	/// \return true si toutes les conditions sont satisfaites.
	bool MatchesRules(const engine::world::terrain::TerrainChunk& chunk,
		uint32_t x, uint32_t z,
		float slopeMinDeg, float slopeMaxDeg,
		float altMin, float altMax);
}
