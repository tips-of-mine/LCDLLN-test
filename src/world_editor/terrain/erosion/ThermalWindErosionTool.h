#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/erosion/ThermalSimulation.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionParams.h"
#include "src/world_editor/terrain/erosion/WindSimulation.h"

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;
}

namespace engine::editor::world::erosion
{
	/// Outil combiné Thermal + Wind Erosion (M100.39). Workflow :
	///   - choisir sous-mode (Thermal seul / Wind seul / Both),
	///   - configurer params, cliquer Simulate,
	///   - en mode Both : Thermal applique sur une copie locale du grid,
	///     Wind utilise le grid post-Thermal comme input,
	///   - cliquer Apply → `ThermalWindErosionCommand` poussée.
	class ThermalWindErosionTool
	{
	public:
		bool Init(engine::editor::world::CommandStack& stack,
			engine::editor::world::TerrainDocument& terrain,
			engine::editor::world::WaterDocument& water,
			const engine::core::Config& cfg);

		void Reset();

		ThermalWindErosionParams&       MutableParams()       { return m_params; }
		const ThermalWindErosionParams& Params()        const { return m_params; }

		const ThermalSimulationResult& LastThermalResult() const { return m_thermalResult; }
		const WindSimulationResult&    LastWindResult()    const { return m_windResult; }
		bool                           HasResult()         const { return m_hasResult; }

		/// Lance la simulation. Bloquant (~0.5-15 s selon paramètres).
		/// Selon `subMode`, exécute Thermal et/ou Wind séquentiellement.
		bool Simulate();

		/// Pousse les deltas cumulés dans une `ThermalWindErosionCommand`.
		/// No-op si `m_hasResult == false`.
		bool Apply();

		/// Abandonne le résultat sans rien pousser.
		void Cancel();

	private:
		engine::editor::world::CommandStack*    m_stack   = nullptr;
		engine::editor::world::TerrainDocument* m_terrain = nullptr;
		engine::editor::world::WaterDocument*   m_water   = nullptr;
		const engine::core::Config*             m_cfg     = nullptr;

		ThermalWindErosionParams m_params;
		ThermalSimulationResult  m_thermalResult;
		WindSimulationResult     m_windResult;
		engine::editor::world::SparseChunkDeltas m_thermalDeltas;
		engine::editor::world::SparseChunkDeltas m_windDeltas;
		bool m_hasResult = false;
	};
}
