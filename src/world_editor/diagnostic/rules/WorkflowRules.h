#pragma once

// M100.49 partie 2 — Règles de diagnostic workflow (catalogue MVP, 10 règles).
// Consolidées dans un seul fichier (au lieu de 10) : chaque règle reste une
// classe distincte enregistrée individuellement, mais on évite l'éparpillement.

#include "src/world_editor/diagnostic/IDiagnosticRule.h"

namespace engine::editor::world::diagnostic
{
	/// Macro interne : déclare une règle simple (id + Run).
#define LCDLLN_DIAG_RULE(ClassName, RuleIdStr)                                  \
	class ClassName : public IDiagnosticRule                                    \
	{                                                                           \
	public:                                                                     \
		const char* GetRuleId() const override { return RuleIdStr; }            \
		void Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const override; \
	}

	LCDLLN_DIAG_RULE(EmptyZoneActiveToolRule,            "workflow.empty_zone_active_tool");
	LCDLLN_DIAG_RULE(NoActiveToolRule,                   "workflow.no_active_tool");
	LCDLLN_DIAG_RULE(ExportAttemptedWithErrorsRule,      "workflow.export_attempted_with_errors");
	LCDLLN_DIAG_RULE(UnsavedChangesLongTimeRule,         "workflow.unsaved_changes_long_time");
	LCDLLN_DIAG_RULE(RiversAfterErosionRule,             "workflow.rivers_after_erosion");
	LCDLLN_DIAG_RULE(ToolSelectedButNoActionRule,        "workflow.tool_selected_but_no_action");
	LCDLLN_DIAG_RULE(PresetJustAppliedNoSaveRule,        "workflow.preset_just_applied_no_save");
	LCDLLN_DIAG_RULE(CavePlacedNoCamouflageRule,         "workflow.cave_placed_no_camouflage");
	LCDLLN_DIAG_RULE(SimpleModeAdvancedAttemptedRule,    "workflow.simple_mode_advanced_features_attempted");
	LCDLLN_DIAG_RULE(NoSeaLevelSetRule,                  "workflow.no_sea_level_set");

#undef LCDLLN_DIAG_RULE
}
