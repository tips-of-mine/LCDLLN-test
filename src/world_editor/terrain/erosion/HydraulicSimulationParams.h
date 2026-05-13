#pragma once

#include <cstdint>

namespace engine::editor::world::erosion
{
	/// Mode d'initialisation des positions des gouttes (M100.38).
	enum class DropletDistribution : uint8_t
	{
		Uniform           = 0,
		WeightedAltitude  = 1,
		WeightedFlowAccum = 2,
	};

	/// Paramètres complets d'une simulation d'érosion hydraulique (M100.38).
	/// Tous les défauts viennent des heuristiques recommandées du ticket.
	struct HydraulicSimulationParams
	{
		uint32_t numDroplets       = 100000u;
		uint32_t maxLifetimeSteps  = 30u;

		// Physique de la goutte.
		float sedimentCapacity      = 4.0f;
		float erosionRate           = 0.3f;
		float depositionRate        = 0.3f;
		float evaporationRate       = 0.01f;
		float gravity               = 4.0f;
		float inertia               = 0.05f;
		float minSlopeForErosion    = 0.01f;
		float maxDeltaPerCellMeters = 2.0f;

		// Initialisation.
		DropletDistribution distribution = DropletDistribution::WeightedAltitude;
		uint32_t            rngSeed      = 42u;

		// Bornes / sécurité.
		bool  stopUnderSeaLevel          = true;
		bool  preserveFlatAreas          = false;
		float flatAreaSlopeThresholdDeg  = 2.0f;
	};
}
