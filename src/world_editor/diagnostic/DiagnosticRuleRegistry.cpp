// M100.49 partie 2 — Implémentation DiagnosticRuleRegistry + enregistrement MVP.

#include "src/world_editor/diagnostic/DiagnosticRuleRegistry.h"

#include "src/world_editor/diagnostic/rules/WorkflowRules.h"

namespace engine::editor::world::diagnostic
{
	void DiagnosticRuleRegistry::RegisterRule(std::unique_ptr<IDiagnosticRule> rule)
	{
		if (!rule) return;
		m_view.push_back(rule.get());
		m_rules.push_back(std::move(rule));
	}

	void RegisterMvpDiagnosticRules(DiagnosticRuleRegistry& registry)
	{
		registry.RegisterRule(std::make_unique<EmptyZoneActiveToolRule>());
		registry.RegisterRule(std::make_unique<NoActiveToolRule>());
		registry.RegisterRule(std::make_unique<ExportAttemptedWithErrorsRule>());
		registry.RegisterRule(std::make_unique<UnsavedChangesLongTimeRule>());
		registry.RegisterRule(std::make_unique<RiversAfterErosionRule>());
		registry.RegisterRule(std::make_unique<ToolSelectedButNoActionRule>());
		registry.RegisterRule(std::make_unique<PresetJustAppliedNoSaveRule>());
		registry.RegisterRule(std::make_unique<CavePlacedNoCamouflageRule>());
		registry.RegisterRule(std::make_unique<SimpleModeAdvancedAttemptedRule>());
		registry.RegisterRule(std::make_unique<NoSeaLevelSetRule>());
	}
}
