#include "src/shardd/gameplay/character/SpawnStatsResolver.h"
#include <algorithm>
namespace engine::server::gameplay
{
	SpawnHealth ResolveSpawnHealth(const CharacterStatsTables& tables,
	                               const std::string& factionId, const std::string& classId,
	                               Sex sex, uint32_t level, uint32_t persistedCurrentHealth)
	{
		auto d = ComputeStats(tables, factionId, classId, sex, level);
		if (!d) return SpawnHealth{}; // resolved=false → l'appelant garde le défaut
		SpawnHealth h;
		h.resolved = true;
		h.maxHealth = d->hp;
		h.currentHealth = (persistedCurrentHealth > 0u)
			? std::min(persistedCurrentHealth, h.maxHealth)
			: h.maxHealth;
		return h;
	}
}
