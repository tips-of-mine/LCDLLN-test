#include "src/world_editor/presets/ToolPresetApply.h"

#include "src/world_editor/splat/SplatPaintTool.h"
#include "src/world_editor/terrain/TerrainBrush.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionParams.h"

#include <algorithm>
#include <cstdint>

namespace engine::editor::world::presets
{
	namespace
	{
		/// Cast tolérant double → uint32 borné à [0, UINT32_MAX].
		uint32_t ToU32(double v)
		{
			if (v < 0.0) return 0u;
			if (v > 4294967295.0) return 4294967295u;
			return static_cast<uint32_t>(v);
		}
	}

	void ApplyHydraulicErosionPreset(
		engine::editor::world::erosion::HydraulicSimulationParams& p,
		const ToolPreset& preset)
	{
		// Chaque GetParam retombe sur la valeur courante de `p` si la clé
		// est absente → un preset partiel ne touche que ce qu'il définit.
		p.numDroplets = ToU32(
			preset.GetParam("numDroplets", static_cast<double>(p.numDroplets)));
		p.maxLifetimeSteps = ToU32(
			preset.GetParam("maxLifetimeSteps", static_cast<double>(p.maxLifetimeSteps)));

		p.sedimentCapacity = static_cast<float>(
			preset.GetParam("sedimentCapacity", p.sedimentCapacity));
		p.erosionRate = static_cast<float>(
			preset.GetParam("erosionRate", p.erosionRate));
		p.depositionRate = static_cast<float>(
			preset.GetParam("depositionRate", p.depositionRate));
		p.evaporationRate = static_cast<float>(
			preset.GetParam("evaporationRate", p.evaporationRate));
		p.gravity = static_cast<float>(
			preset.GetParam("gravity", p.gravity));
		p.inertia = static_cast<float>(
			preset.GetParam("inertia", p.inertia));
		p.minSlopeForErosion = static_cast<float>(
			preset.GetParam("minSlopeForErosion", p.minSlopeForErosion));
		p.maxDeltaPerCellMeters = static_cast<float>(
			preset.GetParam("maxDeltaPerCellMeters", p.maxDeltaPerCellMeters));
	}

	void ApplyThermalWindErosionPreset(
		engine::editor::world::erosion::ThermalWindErosionParams& p,
		const ToolPreset& preset)
	{
		using engine::editor::world::erosion::ErosionSubMode;

		// subMode : 0=Thermal, 1=Wind, 2=Both. Clamp défensif sur l'enum.
		if (preset.HasParam("subMode"))
		{
			const double raw = preset.GetParam("subMode", 2.0);
			const int idx = static_cast<int>(raw);
			p.subMode = static_cast<ErosionSubMode>(std::clamp(idx, 0, 2));
		}

		// --- Bloc thermal (clés pointées thermal.<champ>) ---
		auto& t = p.thermal;
		t.talusAngleDeg = static_cast<float>(
			preset.GetParam("thermal.talusAngleDeg", t.talusAngleDeg));
		t.forcePerPass = static_cast<float>(
			preset.GetParam("thermal.forcePerPass", t.forcePerPass));
		t.numPasses = ToU32(
			preset.GetParam("thermal.numPasses", static_cast<double>(t.numPasses)));
		t.minActivationSlopeDeg = static_cast<float>(
			preset.GetParam("thermal.minActivationSlopeDeg", t.minActivationSlopeDeg));
		if (preset.HasParam("thermal.preserveSteepSlopes"))
		{
			t.preserveSteepSlopes =
				preset.GetParam("thermal.preserveSteepSlopes", 0.0) != 0.0;
		}
		t.preserveSteepThresholdDeg = static_cast<float>(
			preset.GetParam("thermal.preserveSteepThresholdDeg", t.preserveSteepThresholdDeg));

		// --- Bloc wind (clés pointées wind.<champ>) ---
		auto& w = p.wind;
		w.windAngleDeg = static_cast<float>(
			preset.GetParam("wind.windAngleDeg", w.windAngleDeg));
		w.windStrength = static_cast<float>(
			preset.GetParam("wind.windStrength", w.windStrength));
		w.numParticles = ToU32(
			preset.GetParam("wind.numParticles", static_cast<double>(w.numParticles)));
		w.maxLifetimeSteps = ToU32(
			preset.GetParam("wind.maxLifetimeSteps", static_cast<double>(w.maxLifetimeSteps)));
		w.sandCapacityFactor = static_cast<float>(
			preset.GetParam("wind.sandCapacityFactor", w.sandCapacityFactor));
		w.erosionRate = static_cast<float>(
			preset.GetParam("wind.erosionRate", w.erosionRate));
		w.depositionRate = static_cast<float>(
			preset.GetParam("wind.depositionRate", w.depositionRate));
		w.exposureRadiusMeters = static_cast<float>(
			preset.GetParam("wind.exposureRadiusMeters", w.exposureRadiusMeters));
		w.maxDeltaPerCellMeters = static_cast<float>(
			preset.GetParam("wind.maxDeltaPerCellMeters", w.maxDeltaPerCellMeters));
	}

	void ApplySculptPreset(
		engine::editor::world::TerrainBrushParams& p,
		const ToolPreset& preset)
	{
		p.radiusMeters = static_cast<float>(
			preset.GetParam("radiusMeters", p.radiusMeters));
		p.strengthMps = static_cast<float>(
			preset.GetParam("strengthMps", p.strengthMps));
		p.falloff = static_cast<float>(
			preset.GetParam("falloff", p.falloff));
		p.noiseFreq = static_cast<float>(
			preset.GetParam("noiseFreq", p.noiseFreq));
		if (preset.HasParam("noiseOctaves"))
		{
			const double v = preset.GetParam("noiseOctaves", 3.0);
			const int clamped = std::clamp(static_cast<int>(v), 1, 6);
			p.noiseOctaves = static_cast<uint8_t>(clamped);
		}
	}

	void ApplySplatPaintPreset(
		engine::editor::world::SplatPaintParams& p,
		const ToolPreset& preset)
	{
		p.radiusMeters = static_cast<float>(
			preset.GetParam("radiusMeters", p.radiusMeters));
		p.strength = static_cast<float>(
			preset.GetParam("strength", p.strength));
		p.falloff = static_cast<float>(
			preset.GetParam("falloff", p.falloff));
	}
}
