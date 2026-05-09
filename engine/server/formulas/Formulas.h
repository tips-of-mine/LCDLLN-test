#pragma once
// CMANGOS.40 (Phase 4.40a) — Formulas : table de constantes/formules de jeu
// (XP par niveau, drop bonus, dist agro, base stats par classe). Header-only.

#include <cstdint>
#include <algorithm>

namespace engine::server::formulas
{
	/// XP requis pour passer du niveau \p level au level+1.
	/// Approximation cubique douce : 100 * level^2 + 200 * level.
	/// Cap a niveau 80 (XP = 0 au-dela).
	inline uint32_t XpToNextLevel(uint8_t level)
	{
		if (level == 0 || level >= 80) return 0;
		const uint32_t l = level;
		return 100u * l * l + 200u * l;
	}

	/// Distance d'agro (yards) selon difference de niveau (mob - player).
	/// Mob plus haut niveau => detecte de plus loin.
	inline float AggroRadiusYards(int8_t levelDiff)
	{
		// base 20 yards, +/- 1 yard par niveau, clamp [5, 45].
		float r = 20.0f + static_cast<float>(levelDiff);
		if (r < 5.0f)  r = 5.0f;
		if (r > 45.0f) r = 45.0f;
		return r;
	}

	/// Bonus de drop (multiplicateur) en fonction du nombre de joueurs en groupe.
	/// 1 joueur = 1.0, 2 = 1.0, 3+ = 1.0 (le drop ne change pas, c'est le partage
	/// qui change). Mais si le mob est elite, x1.5. Petite formule consolidee.
	inline float DropQuantityMultiplier(uint8_t partySize, bool eliteMob)
	{
		float m = 1.0f;
		if (eliteMob) m *= 1.5f;
		if (partySize >= 5) m *= 1.1f; // raid bonus minimal
		return m;
	}

	/// Base health par niveau pour une classe approximative (warrior).
	/// Utilise comme baseline avant stat bonuses.
	inline uint32_t WarriorBaseHealth(uint8_t level)
	{
		if (level == 0) return 0;
		// 100 base + 50 par niveau, +5 par niveau au-dessus de 60.
		uint32_t hp = 100u + 50u * level;
		if (level > 60) hp += 5u * (level - 60u);
		return hp;
	}
}
