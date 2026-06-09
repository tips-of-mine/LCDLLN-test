#pragma once
// LevelProgression : applique un gain d'XP et fait monter le niveau tant que le
// seuil XpToNextLevel est franchi. Pur et testable (séparé de la boucle réseau).
#include <cstdint>
namespace engine::server::gameplay
{
	/// Résultat d'un gain d'XP.
	struct LevelGainResult
	{
		uint32_t newLevel = 1;        ///< niveau après application.
		uint32_t newXpIntoLevel = 0;  ///< XP restante dans le niveau courant.
		uint32_t levelsGained = 0;    ///< nombre de niveaux gagnés (0 = aucun).
	};

	/// Applique \p gainedXp à un personnage de niveau \p level ayant déjà
	/// \p xpIntoLevel XP dans ce niveau. Monte le niveau tant que le seuil est
	/// franchi (multi-niveaux possible). Au cap \p levelMax, la progression
	/// s'arrête et l'XP dans le niveau est figée à 0 (surplus ignoré).
	/// \param xpBase / \param xpFactor : paramètres de la courbe (character_stats.json).
	LevelGainResult ApplyXpGain(uint32_t level, uint32_t xpIntoLevel, uint32_t gainedXp,
	                            double xpBase, double xpFactor, uint32_t levelMax);
}
