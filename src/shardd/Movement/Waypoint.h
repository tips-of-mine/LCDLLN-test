#pragma once
// Wave 24 — Waypoint : un point d'une patrouille creature_movement. Donnees
// statiques charges depuis la table creature_movement (migration 0062).
//
// Chaque waypoint a un index dans la patrouille (point_idx, monotone), une
// position 3D, un temps d'attente avant le suivant (wait_ms), et un
// optionnel script_id (DBScript a executer au passage).

#include "src/shardd/Movement/INavmeshProvider.h"

#include <cstdint>

namespace engine::server::movement
{
	using CreatureGuid = uint64_t;
	using ScriptId     = uint32_t;

	struct Waypoint
	{
		CreatureGuid creatureGuid = 0;
		uint32_t     pointIdx     = 0;
		PathPoint    position{};
		uint32_t     waitMs       = 0;     ///< temps d'attente avant le waypoint suivant
		ScriptId     scriptId     = 0;     ///< 0 = pas de script
	};
}
