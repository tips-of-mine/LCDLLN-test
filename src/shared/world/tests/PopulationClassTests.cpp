// POP.1 — tests des classes de population (lecture, règle d'or, déduction, résolution).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shared/world/PopulationClass.h"

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

	using namespace engine::world;

	// Valeurs figées : vérifiées à la compilation (formats et réseau futurs).
	static_assert(static_cast<int>(PopulationClass::Interactive) == 0);
	static_assert(static_cast<int>(PopulationClass::Combat) == 1);
	static_assert(static_cast<int>(PopulationClass::Extra) == 2);
	static_assert(static_cast<int>(PopulationClass::Crowd) == 3);
	static_assert(static_cast<int>(PopulationClass::Fauna) == 4);
	static_assert(kPopulationClassCount == 5);
}

int main()
{
	// 1. Lettre et nom : aller-retour pour chaque classe.
	for (std::size_t i = 0; i < kPopulationClassCount; ++i)
	{
		const PopulationClass c = static_cast<PopulationClass>(i);
		const char letter[2] = { PopulationClassLetter(c), '\0' };
		Check(ParsePopulationClass(letter) == c, "lettre → classe");
		Check(ParsePopulationClass(PopulationClassName(c)) == c, "nom → classe");
	}
	Check(PopulationClassLetter(PopulationClass::Interactive) == 'A', "A = interactif");
	Check(PopulationClassLetter(PopulationClass::Fauna) == 'E', "E = faune");

	// 2. Lecture : casse de la lettre indifférente, valeurs inconnues refusées.
	Check(ParsePopulationClass("c") == PopulationClass::Extra, "minuscule acceptée");
	Check(!ParsePopulationClass("F").has_value(), "F inconnue");
	Check(!ParsePopulationClass("").has_value(), "vide refusé");
	Check(!ParsePopulationClass("Combat").has_value(), "nom : minuscules uniquement");
	Check(!ParsePopulationClass("mob").has_value(), "nom inconnu");

	// 3. Règle d'or : seules A et B sont ciblables ; C, D, E délestables.
	Check(IsPopulationTargetable(PopulationClass::Interactive) && IsPopulationTargetable(PopulationClass::Combat), "A et B ciblables");
	Check(!IsPopulationTargetable(PopulationClass::Extra) && !IsPopulationTargetable(PopulationClass::Crowd)
		&& !IsPopulationTargetable(PopulationClass::Fauna), "C, D, E non ciblables");
	Check(!IsPopulationSheddable(PopulationClass::Interactive) && !IsPopulationSheddable(PopulationClass::Combat), "A et B jamais délestés");
	Check(IsPopulationSheddable(PopulationClass::Extra) && IsPopulationSheddable(PopulationClass::Fauna), "C et E délestables");

	// 4. Côté serveur : A, B, C ; D et E n'existent que sur le client.
	Check(IsPopulationServerSide(PopulationClass::Extra), "C sur le shard");
	Check(!IsPopulationServerSide(PopulationClass::Crowd) && !IsPopulationServerSide(PopulationClass::Fauna), "D, E client seul");

	// 5. Priorité : A > B > C > D > E.
	Check(PopulationPriorityRank(PopulationClass::Interactive) < PopulationPriorityRank(PopulationClass::Combat)
		&& PopulationPriorityRank(PopulationClass::Combat) < PopulationPriorityRank(PopulationClass::Extra)
		&& PopulationPriorityRank(PopulationClass::Extra) < PopulationPriorityRank(PopulationClass::Crowd)
		&& PopulationPriorityRank(PopulationClass::Crowd) < PopulationPriorityRank(PopulationClass::Fauna), "ordre de priorité");

	// 6. Déduction (mob sans déclaration).
	Check(InferShardPopulationClass(false, true, false) == PopulationClass::Combat, "créature → B");
	Check(InferShardPopulationClass(true, false, false) == PopulationClass::Extra, "PNJ d'ambiance pacifique → C");
	Check(InferShardPopulationClass(true, true, false) == PopulationClass::Combat, "ambiance attaquable → B");
	Check(InferShardPopulationClass(true, false, true) == PopulationClass::Combat, "garde hostile (Légion) → B");

	// 7. Résolution (déclaration du spawner + règle d'or).
	{
		bool rejected = true;
		Check(ResolveShardPopulationClass(std::nullopt, PopulationClass::Extra, false, false, rejected) == PopulationClass::Extra
			&& !rejected, "absente → déduite");
		Check(ResolveShardPopulationClass(PopulationClass::Interactive, PopulationClass::Extra, false, false, rejected)
			== PopulationClass::Interactive && !rejected, "promotion C → A acceptée");
		Check(ResolveShardPopulationClass(PopulationClass::Extra, PopulationClass::Combat, true, false, rejected)
			== PopulationClass::Combat && rejected, "C sur mob attaquable → B, signalé");
		Check(ResolveShardPopulationClass(PopulationClass::Extra, PopulationClass::Combat, false, true, rejected)
			== PopulationClass::Combat && rejected, "C sur garde hostile → B, signalé");
		Check(ResolveShardPopulationClass(PopulationClass::Crowd, PopulationClass::Combat, true, false, rejected)
			== PopulationClass::Combat && rejected, "D sur le shard → déduite, signalé");
		Check(ResolveShardPopulationClass(PopulationClass::Combat, PopulationClass::Extra, false, false, rejected)
			== PopulationClass::Combat && !rejected, "B forcée sur un PNJ d'ambiance : acceptée");
	}

	if (s_failCount == 0)
		std::printf("[OK] population_class_tests\n");
	return s_failCount == 0 ? 0 : 1;
}
