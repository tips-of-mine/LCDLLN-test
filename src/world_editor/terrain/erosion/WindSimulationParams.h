#pragma once

#include <cstdint>

namespace engine::editor::world::erosion
{
	/// Paramètres de la passe d'érosion éolienne (M100.39). Particle system
	/// avec direction de transport fixe (vent dominant) ; les particules
	/// érodent les faces exposées au vent et déposent sur les faces abritées.
	struct WindSimulationParams
	{
		float    windAngleDeg          = 180.0f;   // N → S par défaut
		float    windStrength          = 0.5f;
		uint32_t numParticles          = 30000u;
		uint32_t maxLifetimeSteps      = 40u;

		float sandCapacityFactor       = 0.3f;
		float erosionRate              = 0.3f;
		float depositionRate           = 0.3f;
		float exposureRadiusMeters     = 30.0f;
		float maxDeltaPerCellMeters    = 1.0f;
		uint32_t rngSeed               = 42u;

		bool stopUnderSeaLevel         = true;
		bool restrictToSandSplat       = false;
		uint8_t sandSplatLayerIndex    = 0u;
	};
}
