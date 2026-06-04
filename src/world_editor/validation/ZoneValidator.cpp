// M100.48 — Implémentation ZoneValidator.

#include "src/world_editor/validation/ZoneValidator.h"

#include <algorithm>

namespace engine::editor::world::validation
{
	namespace
	{
		bool RuleApplies(const IValidationRule& rule, const ZoneValidator::Options& opt)
		{
			if (opt.excludedRules.count(rule.GetRuleId()) != 0) return false;
			if (opt.onlyCategory && opt.category != rule.GetCategory()) return false;
			return true;
		}

		/// True si au moins un tag de la règle figure dans changedTags.
		bool RuleTouchedBy(const IValidationRule& rule, const std::vector<std::string>& changedTags)
		{
			const auto tags = rule.GetDocumentTags();
			for (const auto& t : tags)
				for (const auto& ct : changedTags)
					if (t == ct) return true;
			return false;
		}

		void RecountAndSort(ZoneValidator::Report& report, Severity minSeverity)
		{
			// Filtre par sévérité minimale.
			report.issues.erase(
				std::remove_if(report.issues.begin(), report.issues.end(),
					[minSeverity](const ValidationIssue& i) {
						return static_cast<uint8_t>(i.severity) < static_cast<uint8_t>(minSeverity);
					}),
				report.issues.end());

			// Tri par sévérité décroissante (Error d'abord). Tri stable pour
			// préserver l'ordre d'émission au sein d'une même sévérité.
			std::stable_sort(report.issues.begin(), report.issues.end(),
				[](const ValidationIssue& a, const ValidationIssue& b) {
					return static_cast<uint8_t>(a.severity) > static_cast<uint8_t>(b.severity);
				});

			report.errorCount = report.warningCount = report.hintCount = 0;
			for (const auto& i : report.issues)
			{
				switch (i.severity)
				{
				case Severity::Error:   ++report.errorCount;   break;
				case Severity::Warning: ++report.warningCount; break;
				case Severity::Hint:    ++report.hintCount;    break;
				}
			}
		}
	}

	ZoneValidator::Report ZoneValidator::Validate(const ValidationContext& ctx, const Options& options) const
	{
		Report report;
		for (IValidationRule* rule : m_registry.GetAllRules())
		{
			if (!rule || !RuleApplies(*rule, options)) continue;
			rule->Run(ctx, report.issues);
		}
		RecountAndSort(report, options.minSeverity);
		return report;
	}

	ZoneValidator::Report ZoneValidator::ValidateIncremental(const ValidationContext& ctx,
		const std::vector<std::string>& changedTags,
		const Report& previousReport,
		const Options& options) const
	{
		Report report;

		// Conserve du rapport précédent les problèmes des règles NON touchées.
		// (Une règle est "touchée" si un de ses tags a changé.)
		std::set<std::string> rerunRuleIds;
		for (IValidationRule* rule : m_registry.GetAllRules())
		{
			if (!rule || !RuleApplies(*rule, options)) continue;
			if (RuleTouchedBy(*rule, changedTags))
				rerunRuleIds.insert(rule->GetRuleId());
		}

		for (const auto& issue : previousReport.issues)
		{
			if (rerunRuleIds.count(issue.ruleId) == 0)
				report.issues.push_back(issue); // règle non re-runée : on garde.
		}

		// Re-run les règles touchées.
		for (IValidationRule* rule : m_registry.GetAllRules())
		{
			if (!rule || !RuleApplies(*rule, options)) continue;
			if (rerunRuleIds.count(rule->GetRuleId()) != 0)
				rule->Run(ctx, report.issues);
		}

		RecountAndSort(report, options.minSeverity);
		return report;
	}
}
