// Tests de LevelProgression::ApplyXpGain : montée de niveau pure, ancrée sur la
// vraie courbe XpToNextLevel (paramètres issus du JSON embarqué). Sans assert
// (NDEBUG-safe) : chaque vérif renvoie 1 et logge sur stderr en cas d'échec.
#include "src/shardd/gameplay/character/LevelProgression.h"
#include "src/shared/formulas/Formulas.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include "src/shared/core/Log.h"

#include "CharacterStatsData.h"  // kCharacterStatsJson (généré)
#include "FactionsData.h"        // kFactionsJson (généré)

#include <cstdio>

namespace
{
	using namespace engine::server::gameplay;

	// Seuil XP réel pour passer du niveau \p lvl au suivant (même formule que
	// le code testé, pour des ancres auto-cohérentes).
	uint32_t Threshold(uint32_t lvl, double base, double factor, uint32_t levelMax)
	{
		return engine::server::formulas::XpToNextLevel(
			static_cast<uint8_t>(lvl), base, factor, static_cast<uint8_t>(levelMax));
	}
}

int main()
{
	engine::core::LogSettings s; s.level = engine::core::LogLevel::Info; s.console = true;
	engine::core::Log::Init(s);

	auto tables = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
	if (!tables)
	{
		fprintf(stderr, "[LevelProgressionTests] FromEmbedded a échoué\n");
		engine::core::Log::Shutdown();
		return 1;
	}
	const double base   = tables->xpBase;
	const double factor = tables->xpFactor;
	const uint32_t max  = tables->levelMax;

	// 1) Petit gain sous le seuil : aucun niveau gagné.
	{
		auto r = ApplyXpGain(1, 0, 1, base, factor, max);
		if (r.levelsGained != 0u || r.newLevel != 1u || r.newXpIntoLevel != 1u)
		{
			fprintf(stderr, "[LevelProgressionTests] T1 KO: lvl=%u gained=%u xp=%u\n",
			        r.newLevel, r.levelsGained, r.newXpIntoLevel);
			engine::core::Log::Shutdown();
			return 1;
		}
	}

	// 2) Gain franchissant exactement un seuil : +1 niveau, Xp restante 0.
	{
		const uint32_t g = Threshold(1, base, factor, max);
		auto r = ApplyXpGain(1, 0, g, base, factor, max);
		if (r.newLevel != 2u || r.levelsGained != 1u || r.newXpIntoLevel != 0u)
		{
			fprintf(stderr, "[LevelProgressionTests] T2 KO: g=%u lvl=%u gained=%u xp=%u\n",
			        g, r.newLevel, r.levelsGained, r.newXpIntoLevel);
			engine::core::Log::Shutdown();
			return 1;
		}
	}

	// 3) Un seuil + reliquat : +1 niveau, reliquat conservé.
	{
		const uint32_t g = Threshold(1, base, factor, max) + 5u;
		auto r = ApplyXpGain(1, 0, g, base, factor, max);
		if (r.newLevel != 2u || r.levelsGained != 1u || r.newXpIntoLevel != 5u)
		{
			fprintf(stderr, "[LevelProgressionTests] T3 KO: g=%u lvl=%u gained=%u xp=%u\n",
			        g, r.newLevel, r.levelsGained, r.newXpIntoLevel);
			engine::core::Log::Shutdown();
			return 1;
		}
	}

	// 4) Gros gain franchissant plusieurs niveaux (1->4) : +3 niveaux, Xp 0.
	{
		const uint32_t g = Threshold(1, base, factor, max)
		                 + Threshold(2, base, factor, max)
		                 + Threshold(3, base, factor, max);
		auto r = ApplyXpGain(1, 0, g, base, factor, max);
		if (r.newLevel != 4u || r.levelsGained != 3u || r.newXpIntoLevel != 0u)
		{
			fprintf(stderr, "[LevelProgressionTests] T4 KO: g=%u lvl=%u gained=%u xp=%u\n",
			        g, r.newLevel, r.levelsGained, r.newXpIntoLevel);
			engine::core::Log::Shutdown();
			return 1;
		}
	}

	// 5) Au cap : aucune progression, surplus ignoré.
	{
		auto r = ApplyXpGain(max, 0, 1000000, base, factor, max);
		if (r.newLevel != max || r.levelsGained != 0u || r.newXpIntoLevel != 0u)
		{
			fprintf(stderr, "[LevelProgressionTests] T5 KO: lvl=%u gained=%u xp=%u (max=%u)\n",
			        r.newLevel, r.levelsGained, r.newXpIntoLevel, max);
			engine::core::Log::Shutdown();
			return 1;
		}
	}

	LOG_INFO(Core, "[LevelProgressionTests] ALL OK");
	engine::core::Log::Shutdown();
	return 0;
}
