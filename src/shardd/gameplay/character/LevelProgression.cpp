#include "src/shardd/gameplay/character/LevelProgression.h"
#include "src/shared/formulas/Formulas.h"
#include <algorithm>
namespace engine::server::gameplay
{
	LevelGainResult ApplyXpGain(uint32_t level, uint32_t xpIntoLevel, uint32_t gainedXp,
	                            double xpBase, double xpFactor, uint32_t levelMax)
	{
		LevelGainResult r;
		r.newLevel = (level == 0u) ? 1u : level;   // garde-fou : niveau plancher 1
		// XpToNextLevel prend des uint8_t : la courbe du design plafonne à 100
		// (level_max), donc newLevel (< levelMax dans la boucle) et levelMax tiennent
		// dans un uint8_t. Clamp défensif au cas où la data dépasserait 255.
		const uint8_t levelMaxU8 = static_cast<uint8_t>(std::min<uint32_t>(levelMax, 255u));
		uint64_t xp = static_cast<uint64_t>(xpIntoLevel) + static_cast<uint64_t>(gainedXp);
		while (r.newLevel < levelMax)
		{
			const uint32_t threshold = engine::server::formulas::XpToNextLevel(
				static_cast<uint8_t>(r.newLevel), xpBase, xpFactor, levelMaxU8);
			if (threshold == 0u || xp < static_cast<uint64_t>(threshold))
				break;
			xp -= threshold;
			++r.newLevel;
			++r.levelsGained;
		}
		if (r.newLevel >= levelMax)
			r.newXpIntoLevel = 0u;                  // cap : surplus ignoré
		else
			r.newXpIntoLevel = static_cast<uint32_t>(xp);
		return r;
	}
}
