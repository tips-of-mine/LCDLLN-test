#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationResult.h"

#include <cstdint>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;
}

namespace engine::editor::world::erosion
{
	/// Outil "Hydraulic Erosion" (M100.38). Workflow :
	///   - configurer les paramètres (sliders dans `ToolPropertiesPanel`),
	///   - cliquer Simulate → `RunHydraulicSimulation` exécutée en bloquant,
	///     résultat stocké dans `m_lastResult`,
	///   - cliquer Apply → `HydraulicErosionCommand` poussée sur `CommandStack`,
	///   - cliquer Cancel / Re-simulate → écrase / réinitialise.
	///
	/// Contraintes thread/timing : main thread.
	class HydraulicErosionTool
	{
	public:
		bool Init(engine::editor::world::CommandStack& stack,
			engine::editor::world::TerrainDocument& terrain,
			engine::editor::world::WaterDocument& water,
			const engine::core::Config& cfg);

		void Reset();

		HydraulicSimulationParams&       MutableParams()       { return m_params; }
		const HydraulicSimulationParams& Params()        const { return m_params; }

		const HydraulicSimulationResult& LastResult() const { return m_lastResult; }
		bool                             HasResult()  const { return m_hasResult; }
		bool&                            PreviewEnabled() { return m_previewEnabled; }
		bool                             PreviewEnabledConst() const { return m_previewEnabled; }

		/// Lance la simulation. Bloquant (~1-20 s selon `numDroplets`).
		/// Stocke le résultat dans `m_lastResult` ; remplace tout résultat
		/// précédent. No-op si pas de TerrainDocument câblé.
		bool Simulate();

		/// Pousse `m_lastResult` dans une `HydraulicErosionCommand`. Reset
		/// après push. No-op si `m_hasResult == false`.
		bool Apply();

		/// Abandonne sans rien pousser (reset le buffer + le result).
		void Cancel();

	private:
		engine::editor::world::CommandStack*    m_stack   = nullptr;
		engine::editor::world::TerrainDocument* m_terrain = nullptr;
		engine::editor::world::WaterDocument*   m_water   = nullptr;
		const engine::core::Config*             m_cfg     = nullptr;

		HydraulicSimulationParams m_params;
		HydraulicSimulationResult m_lastResult;
		bool m_hasResult      = false;
		bool m_previewEnabled = true;
	};
}
