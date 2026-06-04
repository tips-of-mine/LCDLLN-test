#pragma once

// M100.48 — ZoneValidator : service principal. Exécute les règles enregistrées
// et produit un rapport trié par sévérité, avec compteurs. Supporte une variante
// incrémentale (ne re-run que les règles touchant les tags modifiés).
//
// Lecture seule sur le contexte ; peut être appelé depuis un worker (les données
// du contexte sont supposées stables pendant la validation).

#include <set>
#include <string>
#include <vector>

#include "src/world_editor/validation/ValidationRuleRegistry.h"
#include "src/world_editor/validation/ValidationTypes.h"

namespace engine::editor::world::validation
{
	class ZoneValidator
	{
	public:
		/// Le validateur référence un registre (non-owning) ; sa durée de vie
		/// doit englober celle du validateur.
		explicit ZoneValidator(const ValidationRuleRegistry& registry) : m_registry(registry) {}

		struct Options
		{
			bool                  onlyCategory = false;
			std::string           category;            ///< Si onlyCategory.
			std::set<std::string> excludedRules;       ///< Règles désactivées (par ruleId).
			Severity              minSeverity = Severity::Hint; ///< Filtre bas.
		};

		struct Report
		{
			std::vector<ValidationIssue> issues; ///< Triés par sévérité décroissante.
			uint32_t errorCount   = 0;
			uint32_t warningCount = 0;
			uint32_t hintCount    = 0;

			/// True si au moins une Error (bloque l'export).
			bool HasBlockingErrors() const { return errorCount > 0; }
		};

		/// Exécute toutes les règles applicables et renvoie un rapport complet.
		Report Validate(const ValidationContext& ctx, const Options& options = Options{}) const;

		/// Variante incrémentale : ne re-run que les règles dont au moins un
		/// GetDocumentTags() est dans `changedTags` ; conserve du rapport
		/// précédent les problèmes des autres règles. Renvoie un rapport fusionné.
		Report ValidateIncremental(const ValidationContext& ctx,
			const std::vector<std::string>& changedTags,
			const Report& previousReport,
			const Options& options = Options{}) const;

	private:
		const ValidationRuleRegistry& m_registry;
	};
}
