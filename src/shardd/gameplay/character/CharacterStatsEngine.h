#pragma once
// CharacterStatsEngine : calcul déterministe des 11 stats à partir des tables
// embarquées + (level, factionId, classId, gender). Pur, compilé shardd-only.

#include "src/shardd/gameplay/character/CharacterStatsTables.h"

#include <cstdint>
#include <optional>
#include <string>

namespace engine::server::gameplay
{
	/// Sexe du personnage.
	enum class Sex : uint8_t { Male, Female };

	/// Résultat : les 11 stats dérivées (valeurs finales, jamais les multiplicateurs).
	struct DerivedStats
	{
		uint32_t hp = 0;            ///< PV max.
		uint32_t resource = 0;      ///< ressource secondaire max.
		uint32_t damage = 0;
		float    accuracy = 0.0f;   ///< précision %.
		float    range = 0.0f;      ///< portée m (0 si mêlée pure).
		float    critRate = 0.0f;   ///< taux de crit % (cap 10).
		float    critMult = 0.0f;   ///< multiplicateur de crit ×.
		float    speedWalk = 0.0f;
		float    speedRun = 0.0f;
		float    speedSprint = 0.0f;
		uint32_t stamina = 0;       ///< endurance max.
		float    perception = 0.0f; ///< m.
		float    stealth = 0.0f;    ///< discrétion m (bas = discret).
		std::string resourceKey;    ///< clé de ressource secondaire (ex. "ferveur").
	};

	/// Calcule les stats pour un personnage. Renvoie nullopt si la faction/classe
	/// est inconnue ou si le profil/la race référencés n'existent pas dans les tables.
	std::optional<DerivedStats> ComputeStats(const CharacterStatsTables& t,
	                                          const std::string& factionId,
	                                          const std::string& classId,
	                                          Sex sex, uint32_t level);
}
