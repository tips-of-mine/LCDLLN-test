// M100.49 partie 2 — Tests du système de diagnostic : registre, chaque règle
// workflow MVP, tri par importance, compteurs, cas « propre ». Headless,
// engine_core.

#include "src/world_editor/diagnostic/DiagnosticRuleRegistry.h"
#include "src/world_editor/diagnostic/DiagnosticSystem.h"

#include <cstdio>
#include <string>

using namespace engine::editor::world::diagnostic;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Has(const DiagnosticSystem::Report& r, const std::string& ruleId)
	{
		for (const auto& s : r.suggestions) if (s.ruleId == ruleId) return true;
		return false;
	}

	const DiagnosticSuggestion* Find(const DiagnosticSystem::Report& r, const std::string& ruleId)
	{
		for (const auto& s : r.suggestions) if (s.ruleId == ruleId) return &s;
		return nullptr;
	}

	void Test_Registry_RegistersTenRules()
	{
		DiagnosticRuleRegistry reg;
		RegisterMvpDiagnosticRules(reg);
		REQUIRE(reg.Count() == 10);
	}

	void Test_EmptyZoneActiveTool()
	{
		DiagnosticRuleRegistry reg; RegisterMvpDiagnosticRules(reg);
		DiagnosticSystem sys(reg);
		DiagnosticContext ctx;
		ctx.hasActiveTool = true;
		ctx.chunkCount = 0;
		auto r = sys.Analyze(ctx);
		REQUIRE(Has(r, "workflow.empty_zone_active_tool"));
		const auto* s = Find(r, "workflow.empty_zone_active_tool");
		REQUIRE(s && s->importance == SuggestionImportance::Strong);
		REQUIRE(s && !s->oneClickActionLabelFr.empty());
	}

	void Test_NoActiveTool()
	{
		DiagnosticRuleRegistry reg; RegisterMvpDiagnosticRules(reg);
		DiagnosticSystem sys(reg);
		DiagnosticContext ctx; // hasActiveTool=false par défaut
		ctx.chunkCount = 5;    // évite empty_zone (qui exige hasActiveTool de toute façon)
		auto r = sys.Analyze(ctx);
		REQUIRE(Has(r, "workflow.no_active_tool"));
	}

	void Test_ExportWithErrors_Critical()
	{
		DiagnosticRuleRegistry reg; RegisterMvpDiagnosticRules(reg);
		DiagnosticSystem sys(reg);
		DiagnosticContext ctx;
		ctx.hasActiveTool = true; ctx.chunkCount = 3;
		ctx.hasUserAttemptedExport = true;
		ctx.validationErrorCount = 2;
		auto r = sys.Analyze(ctx);
		const auto* s = Find(r, "workflow.export_attempted_with_errors");
		REQUIRE(s && s->importance == SuggestionImportance::Critical);
		// Critical doit être trié en tête.
		REQUIRE(!r.suggestions.empty());
		REQUIRE(r.suggestions.front().importance == SuggestionImportance::Critical);
		REQUIRE(r.criticalCount >= 1);
	}

	void Test_UnsavedChanges()
	{
		DiagnosticRuleRegistry reg; RegisterMvpDiagnosticRules(reg);
		DiagnosticSystem sys(reg);
		DiagnosticContext ctx;
		ctx.hasActiveTool = true; ctx.chunkCount = 1;
		ctx.commandsSinceLastSave = 47;
		auto r = sys.Analyze(ctx);
		REQUIRE(Has(r, "workflow.unsaved_changes_long_time"));
		// Seuil : 30 exact ne déclenche pas (> 30).
		DiagnosticContext ctx2; ctx2.hasActiveTool = true; ctx2.chunkCount = 1; ctx2.commandsSinceLastSave = 30;
		REQUIRE(!Has(sys.Analyze(ctx2), "workflow.unsaved_changes_long_time"));
	}

	void Test_RiversAfterErosion_Strong()
	{
		DiagnosticRuleRegistry reg; RegisterMvpDiagnosticRules(reg);
		DiagnosticSystem sys(reg);
		DiagnosticContext ctx;
		ctx.hasActiveTool = true; ctx.chunkCount = 4;
		ctx.erosionAppliedAfterRivers = true;
		auto r = sys.Analyze(ctx);
		const auto* s = Find(r, "workflow.rivers_after_erosion");
		REQUIRE(s && s->importance == SuggestionImportance::Strong);
		REQUIRE(s && !s->confirmationMessageFr.empty()); // action one-click avec confirmation
	}

	void Test_RemainingTipRules()
	{
		DiagnosticRuleRegistry reg; RegisterMvpDiagnosticRules(reg);
		DiagnosticSystem sys(reg);

		{ DiagnosticContext c; c.hasActiveTool = true; c.chunkCount = 1; c.secondsSinceToolSelected = 200.0; c.commandsSinceToolSelected = 0; c.activeToolId = "cave";
		  REQUIRE(Has(sys.Analyze(c), "workflow.tool_selected_but_no_action")); }
		{ DiagnosticContext c; c.hasActiveTool = true; c.chunkCount = 1; c.presetJustAppliedNotSaved = true;
		  REQUIRE(Has(sys.Analyze(c), "workflow.preset_just_applied_no_save")); }
		{ DiagnosticContext c; c.hasActiveTool = true; c.chunkCount = 1; c.cavePlacedWithoutCamouflage = true;
		  REQUIRE(Has(sys.Analyze(c), "workflow.cave_placed_no_camouflage")); }
		{ DiagnosticContext c; c.hasActiveTool = true; c.chunkCount = 1; c.simpleModeActive = true; c.attemptedAdvancedFeature = true;
		  REQUIRE(Has(sys.Analyze(c), "workflow.simple_mode_advanced_features_attempted")); }
		{ DiagnosticContext c; c.hasActiveTool = true; c.chunkCount = 1; c.coastlineToolActive = true; c.seaLevelSet = false;
		  REQUIRE(Has(sys.Analyze(c), "workflow.no_sea_level_set")); }
	}

	void Test_CleanState_NoSuggestions()
	{
		DiagnosticRuleRegistry reg; RegisterMvpDiagnosticRules(reg);
		DiagnosticSystem sys(reg);
		DiagnosticContext ctx;
		// Tout va bien : outil actif, zone non vide, rien d'anormal.
		ctx.hasActiveTool = true;
		ctx.chunkCount = 4;
		ctx.commandsSinceLastSave = 3;
		ctx.seaLevelSet = true;
		auto r = sys.Analyze(ctx);
		REQUIRE(r.IsClean());
		REQUIRE(r.criticalCount == 0 && r.strongCount == 0 && r.tipCount == 0);
	}
}

int main()
{
	Test_Registry_RegistersTenRules();
	Test_EmptyZoneActiveTool();
	Test_NoActiveTool();
	Test_ExportWithErrors_Critical();
	Test_UnsavedChanges();
	Test_RiversAfterErosion_Strong();
	Test_RemainingTipRules();
	Test_CleanState_NoSuggestions();

	if (g_failed == 0)
		std::printf("[diagnostic_tests] all tests passed\n");
	else
		std::fprintf(stderr, "[diagnostic_tests] %d check(s) failed\n", g_failed);
	return g_failed;
}
