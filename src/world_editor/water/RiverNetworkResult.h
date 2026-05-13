#pragma once

#include "src/client/world/water/WaterSurfaces.h"        // RiverInstance, LakeInstance
#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// Résultat immuable d'une simulation watershed (M100.36). Contient les
	/// instances réutilisables par M100.13 (RiverInstance, LakeInstance) et
	/// les deltas de carving optionnels (partage le typedef
	/// `SparseChunkDeltas` introduit par M100.35).
	struct RiverNetworkResult
	{
		std::vector<engine::world::water::RiverInstance> rivers;
		std::vector<engine::world::water::LakeInstance>  autoLakes;
		SparseChunkDeltas                                carveDeltas;

		// Métadonnées pour l'UI résultat. Pas utilisées par la sérialisation.
		uint32_t confluenceCount      = 0;
		uint32_t mouthCount           = 0;
		uint32_t boundaryEndCount     = 0;
		uint32_t sinkEndCount         = 0;
		uint32_t rejectedByThreshold  = 0;
	};
}
