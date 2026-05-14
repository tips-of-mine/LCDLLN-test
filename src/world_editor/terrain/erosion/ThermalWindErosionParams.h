#pragma once

#include "src/world_editor/terrain/erosion/ThermalSimulationParams.h"
#include "src/world_editor/terrain/erosion/WindSimulationParams.h"

#include <cstdint>

namespace engine::editor::world::erosion
{
	/// Sous-mode d'utilisation de l'outil M100.39 : seulement thermal,
	/// seulement wind, ou les deux séquentiellement (thermal d'abord).
	enum class ErosionSubMode : uint8_t
	{
		Thermal = 0,
		Wind    = 1,
		Both    = 2,
	};

	/// Paramètres combinés exposés par `ThermalWindErosionTool`. Le tool
	/// dispatch la simulation selon `subMode`.
	struct ThermalWindErosionParams
	{
		ErosionSubMode subMode = ErosionSubMode::Both;
		ThermalSimulationParams thermal;
		WindSimulationParams    wind;
	};
}
