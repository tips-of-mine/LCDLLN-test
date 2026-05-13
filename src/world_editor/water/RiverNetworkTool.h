#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/water/OceanSettings.h"
#include "src/world_editor/water/RiverNetworkResult.h"
#include "src/world_editor/water/SpringSource.h"
#include "src/world_editor/water/WatershedSimulationParams.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;

	/// Outil "River Network Generator" (M100.36).
	///
	/// Gère :
	///   - liste des sources posées par l'utilisateur (≤ 64),
	///   - paramètres de simulation (threshold, simplification, carving, lacs auto),
	///   - buffer local du slider sea level (écrit dans `WaterDocument` uniquement
	///     à l'Apply, pas pendant le drag du slider),
	///   - bouton `Simulate` qui exécute `RunWatershedSimulation` et stocke
	///     `m_lastResult` pour preview,
	///   - bouton `Apply` qui pousse une `RiverNetworkCommand` sur `CommandStack`.
	///
	/// Contraintes thread/timing : main thread.
	class RiverNetworkTool
	{
	public:
		static constexpr size_t kMaxSprings = 64u;

		bool Init(CommandStack& stack, TerrainDocument& terrain,
			WaterDocument& water, const engine::core::Config& cfg);

		void Reset();

		/// Pose une source à la coordonnée monde XZ donnée. Précondition :
		/// `worldY` doit être la hauteur sampled (déjà raycastée).
		/// Retourne false si la limite `kMaxSprings` est atteinte.
		bool AddSpring(float worldX, float worldZ, float worldY);

		/// Retire la source d'index `idx`. No-op si idx invalide.
		void RemoveSpring(size_t idx);

		/// Déplace la source `idx` (worldX/Z). Recalcule pas Y (l'appelant
		/// doit raycaster). No-op si idx invalide.
		void MoveSpring(size_t idx, float worldX, float worldZ, float worldY);

		const std::vector<SpringSource>& Springs() const { return m_params.springs; }

		WatershedSimulationParams&       MutableParams()       { return m_params; }
		const WatershedSimulationParams& Params()        const { return m_params; }

		/// Buffer local du slider sea level. Initialisé à
		/// `WaterDocument::GetOcean().seaLevelMeters` au premier `Init`,
		/// puis suit le slider de l'UI. Le buffer n'est écrit dans
		/// `WaterDocument` qu'à l'`Apply`.
		float  SeaLevelBuffer() const { return m_seaLevelBuffer; }
		void   SetSeaLevelBuffer(float v) { m_seaLevelBuffer = v; }

		/// Exécute la simulation et stocke le résultat dans `m_lastResult`.
		/// No-op si pas de sources.
		bool Simulate();

		/// Pousse une `RiverNetworkCommand` sur `CommandStack` avec
		/// `m_lastResult`. Reset la liste des sources et le résultat.
		bool Apply();

		/// Abandonne sans pousser. Vide sources + result + reset slider sea
		/// level à la valeur courante du document.
		void Cancel();

		const RiverNetworkResult& LastResult() const { return m_lastResult; }
		bool                      HasResult() const { return m_hasResult; }

	private:
		CommandStack*               m_stack = nullptr;
		TerrainDocument*            m_terrain = nullptr;
		WaterDocument*              m_water = nullptr;
		const engine::core::Config* m_cfg = nullptr;
		WatershedSimulationParams   m_params;
		float                       m_seaLevelBuffer = 50.0f;
		RiverNetworkResult          m_lastResult;
		bool                        m_hasResult = false;
	};
}
