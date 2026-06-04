// M100.50 — Tests Quick Start Wizard : machine d'état, résolution template →
// ZonePreset (toutes combinaisons valides), conditions "if", substitution de
// variables, déterminisme des auto-générateurs. Headless, engine_core.

#include "src/world_editor/wizard/AutoGenerators.h"
#include "src/world_editor/wizard/ConditionEvaluator.h"
#include "src/world_editor/wizard/QuickStartWizard.h"
#include "src/world_editor/wizard/WizardTemplateResolver.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace engine::editor::world::wizard;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool HasOpType(const engine::editor::world::zone_presets::ZonePreset& p, const std::string& type)
	{
		for (const auto& op : p.operations) if (op.type == type) return true;
		return false;
	}

	void Test_Wizard_StateMachine()
	{
		QuickStartWizard w;
		REQUIRE(w.CurrentStep() == WizardStep::Climate);

		// Choix invalide refusé.
		REQUIRE(!w.SetChoiceForCurrentStep("banana"));
		REQUIRE(w.SetChoiceForCurrentStep("arid"));
		REQUIRE(w.Choices().climate == "arid");
		REQUIRE(w.Next());
		REQUIRE(w.CurrentStep() == WizardStep::Relief);

		REQUIRE(w.SetChoiceForCurrentStep("mountains"));
		REQUIRE(w.Next());
		REQUIRE(w.SetChoiceForCurrentStep("dramatic")); // coast
		REQUIRE(w.Next());
		REQUIRE(w.SetChoiceForCurrentStep("dungeon"));   // poi
		REQUIRE(w.Next());
		REQUIRE(w.CurrentStep() == WizardStep::Preview);
		REQUIRE(!w.Next()); // dernière étape
		REQUIRE(w.IsReadyToGenerate());

		// Prev fonctionne.
		REQUIRE(w.Prev());
		REQUIRE(w.CurrentStep() == WizardStep::Poi);
		REQUIRE(!w.IsReadyToGenerate()); // plus à Preview

		w.SetSeed(123u);
		REQUIRE(w.Choices().seed == 123u);
	}

	void Test_Resolver_AllCombinations_ProduceValidPresets()
	{
		WizardTemplateResolver resolver;
		const char* climates[] = { "temperate", "arid", "polar", "tropical" };
		const char* reliefs[]  = { "plains", "hills", "mountains", "escarped" };
		const char* coasts[]   = { "interior", "moderate", "dramatic" };
		const char* pois[]     = { "none", "cave", "ruin", "dungeon" };
		int count = 0, valid = 0;
		for (const char* cl : climates)
			for (const char* r : reliefs)
				for (const char* co : coasts)
					for (const char* po : pois)
					{
						WizardChoices ch; ch.climate = cl; ch.relief = r; ch.coast = co; ch.poi = po;
						auto preset = resolver.Resolve(ch);
						std::string err;
						++count;
						if (preset.Validate(err)) ++valid;
						else std::fprintf(stderr, "  invalide %s/%s/%s/%s: %s\n", cl, r, co, po, err.c_str());
					}
		REQUIRE(count == 192);
		REQUIRE(valid == 192);
	}

	void Test_Resolver_ConditionIf_SkipsOperation()
	{
		WizardTemplateResolver resolver;
		// poi == none → pas de place_*. coast == interior → pas de coastline.
		WizardChoices a; a.poi = "none"; a.coast = "interior";
		auto pa = resolver.Resolve(a);
		REQUIRE(!HasOpType(pa, "place_cave"));
		REQUIRE(!HasOpType(pa, "place_dungeon"));
		REQUIRE(!HasOpType(pa, "place_arch"));
		REQUIRE(!HasOpType(pa, "coastline"));

		// poi == cave → place_cave présent ; coast dramatic → coastline présent.
		WizardChoices b; b.poi = "cave"; b.coast = "dramatic";
		auto pb = resolver.Resolve(b);
		REQUIRE(HasOpType(pb, "place_cave"));
		REQUIRE(HasOpType(pb, "coastline"));
	}

	void Test_Resolver_VariableSubstitution_Works()
	{
		WizardTemplateResolver resolver;
		WizardChoices ch; ch.climate = "temperate";
		auto p = resolver.Resolve(ch);
		// Au moins une opération doit contenir la valeur substituée, pas le
		// placeholder.
		bool foundSubstituted = false;
		bool foundPlaceholder = false;
		for (const auto& op : p.operations)
		{
			if (op.rawJson.find("temperate") != std::string::npos) foundSubstituted = true;
			if (op.rawJson.find("{{climate}}") != std::string::npos) foundPlaceholder = true;
		}
		REQUIRE(foundSubstituted);
		REQUIRE(!foundPlaceholder);
	}

	void Test_AutoGenerators_Deterministic_SameSeedSameOutput()
	{
		auto a = GenerateMountainPolyline("mountains", 42u);
		auto b = GenerateMountainPolyline("mountains", 42u);
		REQUIRE(a.size() == b.size());
		REQUIRE(a.size() == 5); // mountains = 5 points
		bool identical = a.size() == b.size();
		for (size_t i = 0; i < a.size() && identical; ++i)
			if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) identical = false;
		REQUIRE(identical);
	}

	void Test_AutoGenerators_DifferentSeeds_DifferentOutputs()
	{
		auto a = GenerateMountainPolyline("mountains", 42u);
		auto b = GenerateMountainPolyline("mountains", 43u);
		bool different = false;
		for (size_t i = 0; i < a.size() && i < b.size(); ++i)
			if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) different = true;
		REQUIRE(different);

		// hills = 3 points, plains = 2.
		REQUIRE(GenerateMountainPolyline("hills", 1u).size() == 3);
		REQUIRE(GenerateMountainPolyline("plains", 1u).size() == 2);
	}

	void Test_Condition_OperatorsAndQuotes()
	{
		WizardChoices ch; ch.poi = "cave"; ch.coast = "interior";
		REQUIRE(EvaluateCondition("poi == 'cave'", ch));
		REQUIRE(!EvaluateCondition("poi == 'none'", ch));
		REQUIRE(EvaluateCondition("poi != 'none'", ch));
		REQUIRE(EvaluateCondition("coast == 'interior'", ch));
		REQUIRE(EvaluateCondition("", ch));           // vide = vrai
		REQUIRE(EvaluateCondition("garbage", ch));    // non reconnu = vrai
	}
}

int main()
{
	Test_Wizard_StateMachine();
	Test_Resolver_AllCombinations_ProduceValidPresets();
	Test_Resolver_ConditionIf_SkipsOperation();
	Test_Resolver_VariableSubstitution_Works();
	Test_AutoGenerators_Deterministic_SameSeedSameOutput();
	Test_AutoGenerators_DifferentSeeds_DifferentOutputs();
	Test_Condition_OperatorsAndQuotes();

	if (g_failed == 0)
		std::printf("[quick_start_wizard_tests] all tests passed\n");
	else
		std::fprintf(stderr, "[quick_start_wizard_tests] %d check(s) failed\n", g_failed);
	return g_failed;
}
