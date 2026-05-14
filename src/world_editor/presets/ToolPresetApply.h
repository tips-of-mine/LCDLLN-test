#pragma once

#include "src/world_editor/presets/ToolPreset.h"

namespace engine::editor::world::erosion
{
	struct HydraulicSimulationParams;
}

namespace engine::editor::world::presets
{
	/// Applique les paramètres d'un `ToolPreset` à un struct d'outil
	/// (M100.45 Phase B). Fonctions pures, sans dépendance ImGui — donc
	/// testables en isolation par `ToolMigrationTests`.
	///
	/// Convention de tolérance : une clé absente du preset laisse le
	/// champ correspondant **inchangé** (cf. `ToolPreset::GetParam` qui
	/// renvoie la valeur courante en fallback). Un preset partiel ne
	/// casse donc jamais l'état de l'outil.

	/// Mappe les clés JSON `tool_presets/hydraulic_erosion.json` vers
	/// `HydraulicSimulationParams`. Clés reconnues : numDroplets,
	/// maxLifetimeSteps, sedimentCapacity, erosionRate, depositionRate,
	/// evaporationRate, gravity, inertia, minSlopeForErosion,
	/// maxDeltaPerCellMeters.
	void ApplyHydraulicErosionPreset(
		engine::editor::world::erosion::HydraulicSimulationParams& params,
		const ToolPreset& preset);
}
