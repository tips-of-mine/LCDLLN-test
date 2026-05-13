#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/erosion/ThermalSimulation.h"
#include "src/world_editor/terrain/erosion/WindSimulation.h"

#include <cstddef>

namespace engine::editor::world
{
	class TerrainDocument;
}

namespace engine::editor::world::erosion
{
	/// Commande combinée Thermal + Wind erosion (M100.39). Porte les deltas
	/// cumulés des deux passes (l'un ou les deux selon `subMode`). Apply
	/// applique tous les deltas + MarkDirty/OnCommit ; Undo inverse strict.
	class ThermalWindErosionCommand final : public ICommand
	{
	public:
		struct Data
		{
			engine::editor::world::SparseChunkDeltas thermalDeltas;
			engine::editor::world::SparseChunkDeltas windDeltas;
			ThermalSimulationResult thermalStats;
			WindSimulationResult    windStats;
		};

		ThermalWindErosionCommand(engine::editor::world::TerrainDocument& terrain,
			Data data);

		const char* GetLabel()           const override { return "Thermal/Wind Erosion"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

		const Data& Get() const { return m_data; }

	private:
		void ApplyDeltas(const engine::editor::world::SparseChunkDeltas& deltas, float sign);

		engine::editor::world::TerrainDocument* m_terrain = nullptr;
		Data m_data;
	};
}
