#pragma once
// ETAPE4.2 (chantier 2.6 du rapport City of Heroes) — étalement des travaux
// périodiques du shard.
//
// Un travail répété sur N objets (spawners, joueurs, annonces…) à une période P (en
// ticks) n'est plus fait pour tous les objets au même tick : l'objet d'identifiant `id`
// est traité au tick t si (t + id) % P == 0. Chaque objet est donc traité exactement une
// fois par période, et la charge se répartit sur les P ticks au lieu d'un pic.
//
// Module PUR, en-tête seul : testé par tick_slicing_tests.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::server::tick_slicing
{
	/// Période (ticks, ≥ 1) d'un travail fait \p hz fois par seconde à \p tickHz.
	/// \p hz ≤ 0 ou ≥ tickHz : chaque tick (période 1).
	inline uint32_t PeriodTicks(uint32_t tickHz, double hz)
	{
		const uint32_t ticks = (std::max)(tickHz, 1u);
		if (!(hz > 0.0) || hz >= static_cast<double>(ticks))
			return 1u;
		const double period = std::round(static_cast<double>(ticks) / hz);
		return static_cast<uint32_t>(std::clamp(period, 1.0, 3600.0 * static_cast<double>(ticks)));
	}

	/// Vrai si l'objet \p id doit être traité au tick \p tick pour une période de
	/// \p periodTicks ticks (une fois par période, étalé par identifiant).
	inline bool IsSliceTurn(uint64_t id, uint32_t tick, uint32_t periodTicks)
	{
		const uint64_t period = (std::max)(periodTicks, 1u);
		return ((static_cast<uint64_t>(tick) + id) % period) == 0u;
	}
}
