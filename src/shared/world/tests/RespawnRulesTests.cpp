/// Tests CPU de la règle d'éligibilité des cimetières (rayon neutre de faction).
/// Couvre : cimetière neutre, zone neutre par distance, restriction de faction
/// au-delà du rayon, joueur sans faction. Pur CPU, ctest Linux.
#include "src/shared/world/RespawnRules.h"

#include <cstdio>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::IsGraveyardEligibleForRespawn;

	void Test_NeutralGraveyardAlwaysEligible()
	{
		// Faction vide ou "-" : neutre partout, même très loin et même pour une faction donnée.
		REQUIRE(IsGraveyardEligibleForRespawn(10000.0f, 100.0f, "", "alliance") == true);
		REQUIRE(IsGraveyardEligibleForRespawn(10000.0f, 100.0f, "-", "horde") == true);
	}

	void Test_WithinRadiusNeutralForAll()
	{
		// Cimetière de faction "alliance", joueur "horde", DANS le rayon → neutre → éligible.
		REQUIRE(IsGraveyardEligibleForRespawn(50.0f, 100.0f, "alliance", "horde") == true);
		// Pile sur la borne (distance == rayon) → encore neutre.
		REQUIRE(IsGraveyardEligibleForRespawn(100.0f, 100.0f, "alliance", "horde") == true);
	}

	void Test_BeyondRadiusFactionRestricted()
	{
		// Au-delà du rayon : éligible seulement pour la faction propriétaire.
		REQUIRE(IsGraveyardEligibleForRespawn(150.0f, 100.0f, "alliance", "alliance") == true);
		REQUIRE(IsGraveyardEligibleForRespawn(150.0f, 100.0f, "alliance", "horde") == false);
	}

	void Test_PlayerWithoutFactionBeyondRadius()
	{
		// Joueur sans faction (legacy) au-delà du rayon d'un cimetière de faction → exclu.
		REQUIRE(IsGraveyardEligibleForRespawn(150.0f, 100.0f, "alliance", "") == false);
		// Mais dans le rayon neutre, il reste éligible.
		REQUIRE(IsGraveyardEligibleForRespawn(50.0f, 100.0f, "alliance", "") == true);
	}

	void Test_ZeroRadiusOnlyOwnerBeyondZero()
	{
		// Rayon neutre nul : à 0 m c'est neutre (<=0), au-delà réservé au propriétaire.
		REQUIRE(IsGraveyardEligibleForRespawn(0.0f, 0.0f, "alliance", "horde") == true);
		REQUIRE(IsGraveyardEligibleForRespawn(0.1f, 0.0f, "alliance", "horde") == false);
	}
}

int main()
{
	Test_NeutralGraveyardAlwaysEligible();
	Test_WithinRadiusNeutralForAll();
	Test_BeyondRadiusFactionRestricted();
	Test_PlayerWithoutFactionBeyondRadius();
	Test_ZeroRadiusOnlyOwnerBeyondZero();
	if (g_failed == 0) { std::printf("[PASS] RespawnRulesTests\n"); return 0; }
	std::printf("[FAIL] RespawnRulesTests: %d failure(s)\n", g_failed);
	return 1;
}
