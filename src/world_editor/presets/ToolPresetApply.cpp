#include "src/world_editor/presets/ToolPresetApply.h"

#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"

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
}
