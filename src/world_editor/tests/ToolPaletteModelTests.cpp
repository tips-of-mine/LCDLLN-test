/// Tests unitaires CPU pour le modèle de la palette d'outils
/// (réorganisation UI 2026-07-17, PR 2).
///
/// Pas d'ImGui ni de shell — la suite tourne sous ctest Linux. On vérifie :
///   - Les 4 familles couvrent exactement les 15 outils de l'enum (chaque
///     valeur 1..15 apparaît UNE fois, aucune n'est oubliée ni dupliquée).
///   - Aucun groupe vide, titres non vides et uniques.
///   - `ToolLabelFr` : libellé non vide et unique pour chaque outil, cas
///     `None` et valeur hors enum définis (pas de nullptr).
///
/// Framework : REQUIRE maison + main monolithique (pattern des autres
/// suites world_editor).

#include "src/world_editor/ui/ToolPaletteModel.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::editor::world::ActiveTool;
	using engine::editor::world::GetToolPaletteGroups;
	using engine::editor::world::kActiveToolCount;
	using engine::editor::world::ToolLabelFr;
	using engine::editor::world::ToolPaletteGroup;

	/// Test : couverture exacte de l'enum — chaque outil 1..15 apparaît une
	/// et une seule fois dans l'union des groupes ; None n'y figure pas.
	void Test_Groups_CoverAllToolsExactlyOnce()
	{
		const std::vector<ToolPaletteGroup>& groups = GetToolPaletteGroups();
		std::set<int> seen;
		size_t total = 0;
		for (const ToolPaletteGroup& g : groups)
		{
			for (ActiveTool t : g.tools)
			{
				const int v = static_cast<int>(t);
				REQUIRE(v >= 1 && v <= kActiveToolCount); // jamais None ni hors enum
				REQUIRE(seen.insert(v).second);           // pas de doublon
				++total;
			}
		}
		REQUIRE(total == static_cast<size_t>(kActiveToolCount));
	}

	/// Test : structure des groupes — 4 familles, non vides, titres uniques.
	void Test_Groups_TitlesAndNonEmpty()
	{
		const std::vector<ToolPaletteGroup>& groups = GetToolPaletteGroups();
		REQUIRE(groups.size() == 4u);
		std::set<std::string> titles;
		for (const ToolPaletteGroup& g : groups)
		{
			REQUIRE(g.titleFr != nullptr && std::strlen(g.titleFr) > 0);
			REQUIRE(titles.insert(g.titleFr).second);
			REQUIRE(!g.tools.empty());
		}
	}

	/// Test : ToolLabelFr — libellés définis, non vides, uniques ; cas None
	/// et hors-enum sûrs (pas de nullptr).
	void Test_ToolLabelFr_DefinedAndUnique()
	{
		std::set<std::string> labels;
		for (int v = 1; v <= kActiveToolCount; ++v)
		{
			const char* label = ToolLabelFr(static_cast<ActiveTool>(v));
			REQUIRE(label != nullptr && std::strlen(label) > 0);
			REQUIRE(labels.insert(label).second);
		}
		REQUIRE(std::strcmp(ToolLabelFr(ActiveTool::None), "Aucun outil") == 0);
		const char* bogus = ToolLabelFr(static_cast<ActiveTool>(99));
		REQUIRE(bogus != nullptr && std::strlen(bogus) > 0);
	}
}

int main()
{
	Test_Groups_CoverAllToolsExactlyOnce();
	Test_Groups_TitlesAndNonEmpty();
	Test_ToolLabelFr_DefinedAndUnique();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[ToolPaletteModelTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[ToolPaletteModelTests] all tests passed\n");
	return 0;
}
