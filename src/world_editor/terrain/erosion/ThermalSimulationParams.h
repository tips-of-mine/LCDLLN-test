#pragma once

#include <cstdint>

namespace engine::editor::world::erosion
{
	/// Paramètres de la passe d'érosion thermique (M100.39). Le terrain se
	/// relaxe vers son angle de repos : quand deux cellules voisines ont une
	/// pente locale supérieure à `talusAngleDeg`, du matériau s'effondre de
	/// la cellule haute vers la cellule basse.
	struct ThermalSimulationParams
	{
		float talusAngleDeg          = 35.0f;
		float forcePerPass           = 0.3f;
		uint32_t numPasses           = 40u;
		float minActivationSlopeDeg  = 1.0f;
		/// Convergence : si le transfert total d'une passe < ce seuil ×
		/// nombre de cellules, on arrête tôt (early-exit).
		float convergenceThresholdMetersPerCell = 1e-6f;

		bool  stopUnderSeaLevel        = false;
		bool  preserveSteepSlopes      = false;
		float preserveSteepThresholdDeg = 60.0f;
	};
}
