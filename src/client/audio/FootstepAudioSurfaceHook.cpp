// M100.33 — Implémentation de la résolution audio de pas (pure).

#include "src/client/audio/FootstepAudioSurfaceHook.h"

namespace engine::audio
{
	AudioEvent ResolveFootstep(const FootstepSurface& surface, const engine::math::Vec3& footPos)
	{
		AudioEvent ev;
		ev.position = footPos;
		ev.pitch = surface.wet ? 1.05f : 1.0f; // modificateur "wet/slush" : léger pitch up

		if (surface.surfaceName == "snow")
			ev.id = surface.wet ? "step_snow_crunch" : "step_snow";
		else if (surface.surfaceName == "water" || surface.surfaceName == "shallow_water")
			ev.id = "step_water";
		else if (surface.surfaceName == "rock")
			ev.id = "step_rock";
		else if (surface.surfaceName == "grass")
			ev.id = "step_grass";
		else if (surface.surfaceName.empty())
			ev.id = "step_default";
		else
			ev.id = "step_" + surface.surfaceName;

		return ev;
	}
}
