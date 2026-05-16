#pragma once

#include "src/world_editor/water/ConsolidatedHeightGrid.h"

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;

	/// Assemble un `ConsolidatedHeightGrid` à partir des chunks chargés du
	/// `TerrainDocument`. MVP M100.36/.37/.38/.39/.46 : 2×2 chunks autour de
	/// l'origine (`chunk_(0,0)`..`chunk_(1,1)`), via `EnsureLoaded` qui charge
	/// depuis disque ou crée un chunk plat 0 m si absent.
	///
	/// Résolution résultante : `2 × (kTerrainResolution - 1) + 1` cellules
	/// par axe (typiquement 1025×1025 pour kTerrainResolution=513), pas
	/// `kTerrainCellSizeMeters`.
	///
	/// Cette fonction remplace 3 copies historiques identiques (M100.37
	/// `CoastlineEditorTool`, M100.39 `ThermalWindErosionTool`, M100.46
	/// `OperationDispatcher`) — toute évolution vers une zone-complète ou un
	/// streaming différent ne touche désormais qu'un seul endroit.
	///
	/// \param terrain  Document terrain. Mute via `EnsureLoaded` (caches).
	/// \param cfg      Source de `paths.content` pour la lecture disque.
	/// \return         Grid prêt pour `RunHydraulicOnGrid`, `RunWatershedOnGrid`,
	///                 `ExtractCoastlineSegments`, etc.
	///
	/// Effet de bord : bloque le main thread le temps du chargement
	/// disque/CPU des 4 chunks. Pas thread-safe (le document non plus).
	ConsolidatedHeightGrid BuildGridFromLoadedChunks(TerrainDocument& terrain,
		const engine::core::Config& cfg);
}
