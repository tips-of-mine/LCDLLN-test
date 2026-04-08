#pragma once

#include "engine/world/WorldModel.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::world
{
	/// Parse un identifiant carte HTML du type `R050C010` (ligne = R, colonne = C).
	bool ParseMacroCellId(std::string_view id, int32_t& outRow, int32_t& outCol, std::string& err);

	/// AABB d’une cellule macro sur le plan XZ (mètres), demi-ouvert comme dans
	/// `docs/world_macro_grid_bridge_v1.md` : X ∈ [col·L,(col+1)·L), Z ∈ [row·L,(row+1)·L).
	struct MacroCellAabb final
	{
		float minX = 0.0f;
		float maxX = 0.0f;
		float minZ = 0.0f;
		float maxZ = 0.0f;
	};

	[[nodiscard]] MacroCellAabb MacroCellAabbMeters(int32_t row, int32_t col, float macroCellSizeMeters);

	/// Tous les `GlobalChunkCoord` dont l’AABB chunk (côté `chunkSizeMeters`) intersecte `macro`.
	/// Résultat trié par (z, x) pour un ordre déterministe.
	void ChunksIntersectingMacroAabb(const MacroCellAabb& macro, float chunkSizeMeters,
	                                 std::vector<GlobalChunkCoord>& outSorted);
}
