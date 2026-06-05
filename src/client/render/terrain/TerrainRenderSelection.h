#pragma once

// src/client/render/terrain/TerrainRenderSelection.h
//
// Phase 0 du chantier C (unification terrain). Décision pure : faut-il
// dessiner le terrain heightmap legacy (`TerrainRenderer`) cette frame ?
//
// Contexte : deux terrains coexistent (heightmap legacy + chunks data-driven).
// Les dessiner ensemble crée du z-fighting (triangles scintillants au sol).
// On rend donc le legacy EXCLUSIF : il n'est dessiné que si les chunks ne
// couvrent pas la scène (carte heightmap-only, ou chunks pas encore streamés).
//
// Fonction pure, sans état ni dépendance Vulkan -> unit-testable.

#include <cstdint>

namespace engine::render
{
	/// Retourne true si le terrain heightmap legacy doit être dessiné cette frame.
	///
	/// \param legacyTerrainValid  `TerrainRenderer::IsValid()` — une heightmap est chargée.
	/// \param geometryHasLoadPass `GeometryPass::HasLoadPass()` — la passe LOAD est dispo.
	/// \param lastFrameChunkDrawCount Nombre de chunks réellement dessinés à la frame
	///        précédente (0 si le renderer chunk est invalide ou n'a rien dessiné).
	/// \return true si on dessine le legacy ; false si les chunks couvrent la scène
	///         (>0 chunk dessiné), pour éviter le z-fighting / l'overdraw.
	inline bool ShouldDrawLegacyTerrain(bool legacyTerrainValid,
		bool geometryHasLoadPass,
		std::uint32_t lastFrameChunkDrawCount)
	{
		const bool chunksCoveredScene = lastFrameChunkDrawCount > 0u;
		return legacyTerrainValid && geometryHasLoadPass && !chunksCoveredScene;
	}
}
