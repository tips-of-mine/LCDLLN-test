#pragma once
// ETAPE2.3 (chantier 3.3) — sauvegardes automatiques étalées.
//
// Avant : tous les personnages étaient sauvés au même tick, toutes les 30 s (pic
// d'écritures). Maintenant chaque personnage a son propre tick dans la période :
// celui dont `id % période == tick % période`. Même fréquence par personnage
// (une fois par période), mais la charge est répartie sur toute la période.
//
// Logique pure, testée par autosave_schedule_tests.

#include <cstdint>

namespace engine::server
{
	/// Vrai si le personnage \p characterKey doit être sauvé au tick \p tick.
	/// \param intervalTicks période en ticks (ex. 30 s × 20 Hz = 600) ; 0 = jamais.
	/// Une clé 0 (personnage sans identifiant persistant) n'est jamais sauvée.
	constexpr bool IsCharacterAutosaveDue(uint64_t characterKey, uint64_t tick, uint32_t intervalTicks) noexcept
	{
		if (intervalTicks == 0u || characterKey == 0u)
		{
			return false;
		}
		return (characterKey % intervalTicks) == (tick % intervalTicks);
	}
}
