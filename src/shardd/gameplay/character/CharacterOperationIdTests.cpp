// ETAPE2.4 — tests de l'identifiant d'opération à deux personnages.
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/gameplay/character/CharacterOperationId.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <set>
#include <string>

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
	using engine::server::MakeCharacterOperationId;

	Check(MakeCharacterOperationId("trade", 1, 1761465600000ull, 42) == "trade-s1-1761465600000-42", "format");

	// Unicité : compteur, milliseconde, shard.
	std::set<std::string> ids;
	ids.insert(MakeCharacterOperationId("trade", 1, 1000, 1));
	ids.insert(MakeCharacterOperationId("trade", 1, 1000, 2));
	ids.insert(MakeCharacterOperationId("trade", 1, 1001, 1));
	ids.insert(MakeCharacterOperationId("trade", 2, 1000, 1));
	ids.insert(MakeCharacterOperationId("wallet_transfer", 1, 1000, 1));
	Check(ids.size() == 5u, "cinq identifiants distincts");

	// Taille maximale : tient dans VARCHAR(80).
	const std::string longest = MakeCharacterOperationId("un_type_vraiment_tres_long_qui_depasse",
		std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max());
	Check(longest.size() <= 80u, "tient dans 80 caractères");

	if (s_failCount == 0)
		std::puts("[OK] character_operation_id_tests");
	return s_failCount == 0 ? 0 : 1;
}
