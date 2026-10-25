// ETAPE1.2 — tests de la règle « un seul propriétaire par personnage ».
// Pattern repo : int main() + vérifications + code retour, sans framework.

#include "src/shared/db/CharacterOwnership.h"

#include <cstdio>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}
}

int main()
{
	using engine::server::db::CharacterOwnershipCheck;
	using engine::server::db::CheckCharacterOwnership;
	using engine::server::db::IsWriteAllowed;

	// Propriétaire courant : même shard, même génération.
	Check(CheckCharacterOwnership(1u, 7u, 1u, 7u) == CharacterOwnershipCheck::Owned, "propriétaire courant");

	// Ré-admission plus récente : l'ancienne génération ne peut plus écrire.
	Check(CheckCharacterOwnership(1u, 8u, 1u, 7u) == CharacterOwnershipCheck::NotOwner, "génération dépassée");

	// Personnage repris par un autre shard.
	Check(CheckCharacterOwnership(2u, 8u, 1u, 7u) == CharacterOwnershipCheck::NotOwner, "autre shard");

	// Shard déclaré mort : le master a remis owner_shard_id à NULL (lu 0).
	Check(CheckCharacterOwnership(0u, 7u, 1u, 7u) == CharacterOwnershipCheck::NotOwner, "verrou libéré (shard mort)");

	// Base et master antérieurs au verrou : comportement historique.
	Check(CheckCharacterOwnership(0u, 0u, 1u, 0u) == CharacterOwnershipCheck::LegacyUnlocked, "legacy sans verrou");

	// Admis sans verrou alors qu'un verrou existe : refus.
	Check(CheckCharacterOwnership(1u, 3u, 1u, 0u) == CharacterOwnershipCheck::NotOwner, "sans génération mais verrou en base");

	Check(IsWriteAllowed(CharacterOwnershipCheck::Owned), "Owned autorise");
	Check(IsWriteAllowed(CharacterOwnershipCheck::LegacyUnlocked), "Legacy autorise");
	Check(!IsWriteAllowed(CharacterOwnershipCheck::NotOwner), "NotOwner refuse");

	if (s_failCount == 0)
		std::puts("[OK] character_ownership_tests");
	return s_failCount == 0 ? 0 : 1;
}
