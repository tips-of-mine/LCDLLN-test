#pragma once

// v1.0.143 — sonde de SURFACE MARCHABLE sous un point (ville-tour de Magistrya).
//
// Les PNJ d'ambiance à altitude réelle (kEntityStateRealHeight : paliers et volées des
// cages d'escalier) ne sont pas collés à la heightmap : le client cherche la surface
// marchable (dalle, marche, sol, terrain) juste sous l'altitude répliquée par le shard,
// par un balayage vertical d'une petite capsule dans le collisionneur du monde.
//
// Module pur (aucun état) : testable avec un CompositeWorldCollider (tests unitaires).

#include "src/client/gameplay/CharacterController.h"

namespace engine::gameplay
{
	/// Cherche la surface marchable sous (\p x, \p z) entre \p fromY + \p above et
	/// \p fromY − \p below (m, monde).
	/// \param world collisionneur du monde (terrain, props, pièces de bâtiment).
	/// \param x,z position horizontale (m, repère du collisionneur).
	/// \param fromY altitude de référence (m) : Y répliqué par le shard (pieds du PNJ).
	/// \param above marge au-dessus (m) : une marche un peu plus haute que fromY.
	/// \param below profondeur maximale de la recherche sous fromY (m).
	/// \param outY reçoit l'altitude de la surface trouvée (m).
	/// \return true si une surface quasi horizontale (normale y > 0,5) est trouvée.
	bool ProbeWalkableSurfaceBelow(const IWorldCollider& world, float x, float z, float fromY,
		float above, float below, float& outY);
}
