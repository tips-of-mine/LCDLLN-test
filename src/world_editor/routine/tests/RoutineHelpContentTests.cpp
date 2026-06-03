// M101.11 — Test : le contenu d'aide des routines parse correctement, headless.
// Vérifie le format des tooltips routine via le parser M100.47 existant.

#include "src/world_editor/help/HelpContentStore.h"

#include <cstdio>
#include <string>

using namespace engine::editor::world::help;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	// JSON représentatif de game/data/editor/tooltips/routine.json.
	const char* kRoutineJson = R"JSON({
  "toolId": "routine",
  "tooltips": {
    "BranchIf": {
      "label": "Branche (si)",
      "description_simple": "Choisit la suite selon une condition.",
      "description_advanced": "Lit un pin Data Bool ; suit true/false.",
      "defaultValue": "",
      "range": "",
      "docSectionId": "routine/nodes#BranchIf"
    },
    "graph": {
      "label": "Graphe de routine",
      "description_simple": "Programme visuel de noeuds.",
      "description_advanced": "Artefact JSON interprete au runtime.",
      "defaultValue": "zone_event",
      "range": "zone_event / npc_routine",
      "docSectionId": "routine/graph"
    }
  }
})JSON";

	void Test_ParseRoutineTooltips()
	{
		TooltipFileContents out;
		std::string err;
		bool ok = ParseTooltipFileJson(kRoutineJson, out, err);
		REQUIRE(ok);
		REQUIRE(out.toolId == "routine");
		REQUIRE(out.tooltips.size() == 2);
		auto it = out.tooltips.find("BranchIf");
		REQUIRE(it != out.tooltips.end());
		if (it != out.tooltips.end())
			REQUIRE(!it->second.label.empty());
	}

	void Test_RejectMissingToolId()
	{
		// Contrat M100.47 : toolId manquant => échec structurel.
		TooltipFileContents out;
		std::string err;
		bool ok = ParseTooltipFileJson("{ \"tooltips\": {} }", out, err);
		REQUIRE(!ok);
	}
}

int main()
{
	Test_ParseRoutineTooltips();
	Test_RejectMissingToolId();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutineHelpContentTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutineHelpContentTests: %d échec(s)\n", g_failed);
	return g_failed;
}
