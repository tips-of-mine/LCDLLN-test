// src/client/character_creation/tests/FactionDefinitionTests.cpp
//
// Système de personnages PR2 — tests des factions chargées depuis
// game/data/races/factions.json via CharacterCreationPresenter.
//
// Vérifie (accesseurs Task 5) :
//   - 10 factions chargées (9 sélectionnables + empire_hynn masquée) ;
//   - GetSelectableFactionIndices() renvoie exactement 9 ;
//   - empire_hynn présente avec selectable == false (exclue des indices) ;
//   - chaque faction sélectionnable a >= 4 classes, toutes avec un id non vide
//     et des ids DISTINCTS au sein de la faction ;
//   - spot-checks de classes spécifiques (elfe → voleur_tenebreux ;
//     lumiere → inquisiteur_chatieur + inquisiteur_hospitalier) ;
//   - mapping faction → race (elfe→elfes, dzorak→orcs, legion→demons).
//
// CTest tourne avec WORKING_DIRECTORY = CMAKE_SOURCE_DIR, donc les chemins
// "game/data/..." sont résolubles tels quels.

#include "src/client/character_creation/CharacterCreationUi.h"
#include "src/shared/core/Config.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using engine::client::CharacterCreationPresenter;
using engine::client::FactionClass;
using engine::client::FactionDefinition;
using engine::core::Config;

namespace
{
	int g_failed = 0;
#define REQUIRE(cond)                                                             \
	do                                                                            \
	{                                                                             \
		if (!(cond))                                                              \
		{                                                                         \
			std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); \
			++g_failed;                                                           \
		}                                                                         \
	} while (0)

	/// Construit un Config minimal pointant vers les JSON réels du repo. Les
	/// tests CTest tournent avec WORKING_DIRECTORY = CMAKE_SOURCE_DIR, donc
	/// "game/data" est résoluble tel quel.
	Config MakeConfigPointingToRepoContent()
	{
		Config cfg;
		cfg.SetValue("paths.content", std::string("game/data"));
		cfg.SetValue("char_creation.races_path", std::string("races/races.json"));
		cfg.SetValue("char_creation.classes_path", std::string("races/classes.json"));
		cfg.SetValue("char_creation.factions_path", std::string("races/factions.json"));
		return cfg;
	}

	/// Cherche l'index d'une faction par id dans GetFactions().
	/// Renvoie -1 si introuvable.
	int FindFactionIndex(const std::vector<FactionDefinition>& factions, const std::string& id)
	{
		for (size_t i = 0; i < factions.size(); ++i)
			if (factions[i].id == id)
				return static_cast<int>(i);
		return -1;
	}

	/// Cherche une classe par id dans une liste de classes de faction.
	/// Renvoie nullptr si introuvable.
	const FactionClass* FindClass(const std::vector<FactionClass>& classes, const std::string& id)
	{
		for (const auto& c : classes)
			if (c.id == id)
				return &c;
		return nullptr;
	}

	/// Vérifie le nombre total de factions et la partition sélectionnable /
	/// non-sélectionnable (empire_hynn masquée).
	void Test_FactionCountsAndSelectability()
	{
		CharacterCreationPresenter p;
		Config cfg = MakeConfigPointingToRepoContent();
		REQUIRE(p.Init(cfg));

		const auto& factions = p.GetFactions();
		// 10 factions au total : 9 sélectionnables + empire_hynn (masquée).
		REQUIRE(factions.size() == 10);

		const auto selectable = p.GetSelectableFactionIndices();
		REQUIRE(selectable.size() == 9);

		const int empireIdx = FindFactionIndex(factions, "empire_hynn");
		REQUIRE(empireIdx >= 0);
		REQUIRE(!factions[static_cast<size_t>(empireIdx)].selectable);

		// empire_hynn ne doit PAS figurer dans les indices sélectionnables.
		for (uint32_t idx : selectable)
		{
			REQUIRE(idx < factions.size());
			REQUIRE(factions[idx].selectable);
			REQUIRE(factions[idx].id != "empire_hynn");
		}
	}

	/// Vérifie que chaque faction sélectionnable a >= 4 classes, toutes avec un
	/// id non vide et des ids DISTINCTS au sein de la faction.
	void Test_SelectableFactionsHaveDistinctClasses()
	{
		CharacterCreationPresenter p;
		Config cfg = MakeConfigPointingToRepoContent();
		REQUIRE(p.Init(cfg));

		const auto selectable = p.GetSelectableFactionIndices();
		for (uint32_t idx : selectable)
		{
			const std::vector<FactionClass>* classes = p.GetFactionClasses(idx);
			REQUIRE(classes != nullptr);
			if (!classes)
				continue;
			REQUIRE(classes->size() >= 4);

			std::set<std::string> seen;
			for (const auto& c : *classes)
			{
				REQUIRE(!c.id.empty());
				// id distinct au sein de la faction.
				REQUIRE(seen.insert(c.id).second);
			}
		}
	}

	/// Spot-checks de classes spécifiques attendues dans certaines factions.
	void Test_SpecificClassSpotChecks()
	{
		CharacterCreationPresenter p;
		Config cfg = MakeConfigPointingToRepoContent();
		REQUIRE(p.Init(cfg));

		const auto& factions = p.GetFactions();

		const int elfeIdx = FindFactionIndex(factions, "elfe");
		REQUIRE(elfeIdx >= 0);
		const std::vector<FactionClass>* elfeClasses =
		    p.GetFactionClasses(static_cast<uint32_t>(elfeIdx));
		REQUIRE(elfeClasses != nullptr);
		if (elfeClasses)
			REQUIRE(FindClass(*elfeClasses, "voleur_tenebreux") != nullptr);

		const int lumiereIdx = FindFactionIndex(factions, "lumiere");
		REQUIRE(lumiereIdx >= 0);
		const std::vector<FactionClass>* lumiereClasses =
		    p.GetFactionClasses(static_cast<uint32_t>(lumiereIdx));
		REQUIRE(lumiereClasses != nullptr);
		if (lumiereClasses)
		{
			REQUIRE(FindClass(*lumiereClasses, "inquisiteur_chatieur") != nullptr);
			REQUIRE(FindClass(*lumiereClasses, "inquisiteur_hospitalier") != nullptr);
		}
	}

	/// Vérifie le mapping faction → race via GetRaceIdForFaction.
	void Test_FactionRaceMapping()
	{
		CharacterCreationPresenter p;
		Config cfg = MakeConfigPointingToRepoContent();
		REQUIRE(p.Init(cfg));

		const auto& factions = p.GetFactions();

		const int elfeIdx = FindFactionIndex(factions, "elfe");
		REQUIRE(elfeIdx >= 0);
		REQUIRE(p.GetRaceIdForFaction(static_cast<uint32_t>(elfeIdx)) == "elfes");

		const int dzorakIdx = FindFactionIndex(factions, "dzorak");
		REQUIRE(dzorakIdx >= 0);
		REQUIRE(p.GetRaceIdForFaction(static_cast<uint32_t>(dzorakIdx)) == "orcs");

		const int legionIdx = FindFactionIndex(factions, "legion");
		REQUIRE(legionIdx >= 0);
		REQUIRE(p.GetRaceIdForFaction(static_cast<uint32_t>(legionIdx)) == "demons");
	}
} // namespace

int main()
{
	Test_FactionCountsAndSelectability();
	Test_SelectableFactionsHaveDistinctClasses();
	Test_SpecificClassSpotChecks();
	Test_FactionRaceMapping();

	if (g_failed == 0)
		std::printf("[OK] FactionDefinitionTests: all assertions passed\n");
	else
		std::fprintf(stderr, "[FAILED] FactionDefinitionTests: %d assertion(s) failed\n", g_failed);
	return g_failed == 0 ? 0 : 1;
}
