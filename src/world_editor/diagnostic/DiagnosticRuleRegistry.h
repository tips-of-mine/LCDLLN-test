#pragma once

// M100.49 partie 2 — Registre des règles de diagnostic (instanciable, pas un
// singleton global = testable). RegisterMvpDiagnosticRules enregistre les
// règles workflow MVP.

#include <memory>
#include <vector>

#include "src/world_editor/diagnostic/IDiagnosticRule.h"

namespace engine::editor::world::diagnostic
{
	class DiagnosticRuleRegistry
	{
	public:
		void RegisterRule(std::unique_ptr<IDiagnosticRule> rule);
		const std::vector<IDiagnosticRule*>& GetAllRules() const { return m_view; }
		size_t Count() const { return m_rules.size(); }

	private:
		std::vector<std::unique_ptr<IDiagnosticRule>> m_rules;
		std::vector<IDiagnosticRule*>                 m_view;
	};

	/// Enregistre les règles workflow MVP dans `registry`.
	void RegisterMvpDiagnosticRules(DiagnosticRuleRegistry& registry);
}
