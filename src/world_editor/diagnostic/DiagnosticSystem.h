#pragma once

// M100.49 partie 2 — DiagnosticSystem : exécute les règles de diagnostic et
// renvoie les suggestions triées par importance décroissante (Critical d'abord).
// Lecture seule ; ne déclenche aucune action.

#include <vector>

#include "src/world_editor/diagnostic/DiagnosticRuleRegistry.h"
#include "src/world_editor/diagnostic/IDiagnosticRule.h"

namespace engine::editor::world::diagnostic
{
	class DiagnosticSystem
	{
	public:
		explicit DiagnosticSystem(const DiagnosticRuleRegistry& registry) : m_registry(registry) {}

		struct Report
		{
			std::vector<DiagnosticSuggestion> suggestions; ///< Triées importance desc.
			uint32_t criticalCount = 0;
			uint32_t strongCount   = 0;
			uint32_t tipCount      = 0;

			/// True si aucune suggestion (« tu as l'air sur les bons rails ! »).
			bool IsClean() const { return suggestions.empty(); }
		};

		/// Analyse l'état courant : exécute toutes les règles, trie, compte.
		Report Analyze(const DiagnosticContext& ctx) const;

	private:
		const DiagnosticRuleRegistry& m_registry;
	};
}
