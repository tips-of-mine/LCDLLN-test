#pragma once

// M100.33 — Mappe (surface, modificateurs) -> évènement audio de pas. Logique
// PURE (le branchement sur SurfaceQuery/CharacterController/AudioSystem réels
// est fait côté intégration, différé). Testable headless.

#include <string>

#include "src/shared/math/Math.h"

namespace engine::audio
{
	struct AudioEvent
	{
		std::string id;
		float pitch = 1.0f;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
	};

	/// Surface sous le pied + modificateurs météo pertinents.
	struct FootstepSurface
	{
		std::string surfaceName; // "grass" / "snow" / "rock" / "water" / ...
		bool wet = false;        // modificateur météo (pluie/neige fondante)
	};

	/// Résout l'évènement audio de pas : id selon la surface (et neige mouillée
	/// = crunch), pitch +5 % si mouillé.
	AudioEvent ResolveFootstep(const FootstepSurface& surface, const engine::math::Vec3& footPos);
}
