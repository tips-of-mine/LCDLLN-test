// M100.49 partie 2 — Implémentation DiagnosticSystem.

#include "src/world_editor/diagnostic/DiagnosticSystem.h"

#include <algorithm>

namespace engine::editor::world::diagnostic
{
	DiagnosticSystem::Report DiagnosticSystem::Analyze(const DiagnosticContext& ctx) const
	{
		Report report;
		for (IDiagnosticRule* rule : m_registry.GetAllRules())
		{
			if (rule) rule->Run(ctx, report.suggestions);
		}

		// Tri stable par importance décroissante (Critical d'abord).
		std::stable_sort(report.suggestions.begin(), report.suggestions.end(),
			[](const DiagnosticSuggestion& a, const DiagnosticSuggestion& b) {
				return static_cast<uint8_t>(a.importance) > static_cast<uint8_t>(b.importance);
			});

		for (const auto& s : report.suggestions)
		{
			switch (s.importance)
			{
			case SuggestionImportance::Critical: ++report.criticalCount; break;
			case SuggestionImportance::Strong:   ++report.strongCount;   break;
			case SuggestionImportance::Tip:      ++report.tipCount;      break;
			}
		}
		return report;
	}
}
