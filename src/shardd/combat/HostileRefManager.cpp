// HostileRefManager : translation unit pour CleanupOnDeath (la seule
// fonction non-inline). Le reste est header-only inline pour permettre
// au compilateur d'optimiser les push/erase dans les call-sites typiques
// (Update tick chaque frame).
#include "src/shardd/combat/HostileRefManager.h"

namespace engine::server::combat
{
	void CleanupOnDeath(EntityId deadId,
		const std::vector<HostileRefManager*>& affectedManagers)
	{
		// Iteration unique : pour chaque manager, retirer deadId des 2 listes
		// (targets ET haters). erase() est no-op si absent, donc safe meme si
		// le manager n'avait pas de relation avec deadId.
		for (auto* mgr : affectedManagers)
		{
			if (mgr == nullptr)
				continue;
			mgr->RemoveTarget(deadId);
			mgr->RemoveHater(deadId);
		}
	}
}
