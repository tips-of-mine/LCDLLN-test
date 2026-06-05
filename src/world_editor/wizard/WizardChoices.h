#pragma once

// M100.50 — Choix de l'assistant Quick Start (5 étapes). Valeurs en chaînes
// (cohérent avec la substitution {{var}} et les conditions "poi == 'none'").

#include <cstdint>
#include <string>

namespace engine::editor::world::wizard
{
	/// Les 4 choix paramétriques + le seed. La prévisualisation (étape 5) ne
	/// modifie que le seed.
	struct WizardChoices
	{
		std::string climate = "temperate"; ///< temperate | arid | polar | tropical
		std::string relief  = "hills";     ///< plains | hills | mountains | escarped
		std::string coast   = "interior";  ///< interior | moderate | dramatic
		std::string poi     = "none";      ///< none | cave | ruin | dungeon
		uint32_t    seed    = 42u;
	};

	/// Valeurs valides par dimension (pour validation + UI).
	bool IsValidClimate(const std::string& v);
	bool IsValidRelief(const std::string& v);
	bool IsValidCoast(const std::string& v);
	bool IsValidPoi(const std::string& v);
}
