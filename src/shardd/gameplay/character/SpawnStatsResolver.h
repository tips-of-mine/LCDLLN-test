#pragma once
// SpawnStatsResolver : résout les PV (max + courants) d'un joueur à l'enter-world
// depuis le moteur de stats. Pur et testable (séparé de HandleHello, intégration).
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include <cstdint>
#include <string>
namespace engine::server::gameplay
{
	/// PV résolus à l'entrée en jeu.
	struct SpawnHealth { bool resolved = false; uint32_t maxHealth = 0; uint32_t currentHealth = 0; };

	/// Calcule maxHealth (= hp dérivé) et currentHealth pour un joueur entrant.
	/// \param persistedCurrentHealth PV courants restaurés (0 = aucun → plein).
	/// \return resolved=false si la faction/classe est inconnue (l'appelant garde le défaut).
	SpawnHealth ResolveSpawnHealth(const CharacterStatsTables& tables,
	                               const std::string& factionId, const std::string& classId,
	                               Sex sex, uint32_t level, uint32_t persistedCurrentHealth);
}
