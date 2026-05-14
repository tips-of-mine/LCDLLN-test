#pragma once

#include "src/world_editor/presets/ToolPreset.h"

namespace engine::editor::world
{
	struct TerrainBrushParams;
	struct SplatPaintParams;
}

namespace engine::editor::world::erosion
{
	struct HydraulicSimulationParams;
	struct ThermalWindErosionParams;
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

	/// Mappe les clés JSON `tool_presets/thermal_wind_erosion.json` vers
	/// `ThermalWindErosionParams`. Les paramètres sont nichés : clés
	/// pointées `thermal.<champ>` / `wind.<champ>` + `subMode` à la
	/// racine (0=Thermal, 1=Wind, 2=Both). Clés reconnues :
	///   subMode,
	///   thermal.talusAngleDeg, thermal.forcePerPass, thermal.numPasses,
	///   thermal.minActivationSlopeDeg, thermal.preserveSteepSlopes,
	///   thermal.preserveSteepThresholdDeg,
	///   wind.windAngleDeg, wind.windStrength, wind.numParticles,
	///   wind.maxLifetimeSteps, wind.sandCapacityFactor,
	///   wind.erosionRate, wind.depositionRate, wind.exposureRadiusMeters,
	///   wind.maxDeltaPerCellMeters.
	void ApplyThermalWindErosionPreset(
		engine::editor::world::erosion::ThermalWindErosionParams& params,
		const ToolPreset& preset);

	/// Mappe les clés JSON `tool_presets/sculpt.json` vers
	/// `TerrainBrushParams`. Clés reconnues : radiusMeters, strengthMps,
	/// falloff, noiseFreq, noiseOctaves. Le `mode` de la brosse n'est pas
	/// touché par un preset (c'est un choix d'interaction de l'utilisateur).
	void ApplySculptPreset(
		engine::editor::world::TerrainBrushParams& params,
		const ToolPreset& preset);

	/// Mappe les clés JSON `tool_presets/splat_paint.json` vers
	/// `SplatPaintParams`. Clés reconnues : radiusMeters, strength,
	/// falloff. `activeLayer` et `autoRules` ne sont pas touchés (choix
	/// d'interaction de l'utilisateur, comme le `mode` du sculpt).
	void ApplySplatPaintPreset(
		engine::editor::world::SplatPaintParams& params,
		const ToolPreset& preset);
}
