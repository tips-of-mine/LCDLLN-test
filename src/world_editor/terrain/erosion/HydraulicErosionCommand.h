#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationResult.h"

#include <cstddef>

namespace engine::editor::world
{
	class TerrainDocument;
}

namespace engine::editor::world::erosion
{
	/// Commande d'érosion hydraulique (M100.38). Applique les `SparseChunkDeltas`
	/// cumulés sur la heightmap via `TerrainDocument`. Undo inverse les
	/// deltas (× −1) bit-à-bit. Le snapshot `paramsSnapshot` est conservé
	/// pour info debug uniquement (non rejoué).
	class HydraulicErosionCommand final : public ICommand
	{
	public:
		HydraulicErosionCommand(engine::editor::world::TerrainDocument& terrain,
			HydraulicSimulationResult result,
			HydraulicSimulationParams paramsSnapshot);

		const char* GetLabel()           const override { return "Hydraulic Erosion"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

		const HydraulicSimulationResult& Result() const { return m_result; }

	private:
		void ApplyDeltas(float sign);

		engine::editor::world::TerrainDocument* m_terrain = nullptr;
		HydraulicSimulationResult m_result;
		HydraulicSimulationParams m_paramsSnapshot;
	};
}
