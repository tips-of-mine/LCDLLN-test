#pragma once

// M100.48 — Règles de validation splat. La SplatMap stocke 8 poids/octet par
// cellule, somme invariante = 255 (cf. SplatMap.h). Les règles vérifient cet
// invariant et signalent les cellules vides.

#include "src/world_editor/validation/IValidationRule.h"

namespace engine::editor::world::validation
{
	/// `splat.sum_invalid` (Error) : la somme des poids d'une cellule s'écarte
	/// de 255 au-delà de la tolérance (invariant cassé → blend incohérent).
	class SplatSumInvalidRule : public IValidationRule
	{
	public:
		const char* GetRuleId() const override { return "splat.sum_invalid"; }
		const char* GetCategory() const override { return "splat"; }
		const char* GetDescription() const override { return "Somme des poids d'une cellule != 255 (invariant cassé)."; }
		Severity GetDefaultSeverity() const override { return Severity::Error; }
		std::vector<std::string> GetDocumentTags() const override { return { "splat" }; }
		void Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const override;
	};

	/// `splat.empty_cell` (Warning) : cellule sans aucune couche peinte (somme=0).
	class SplatEmptyCellRule : public IValidationRule
	{
	public:
		const char* GetRuleId() const override { return "splat.empty_cell"; }
		const char* GetCategory() const override { return "splat"; }
		const char* GetDescription() const override { return "Cellule splat sans aucune couche (somme=0)."; }
		Severity GetDefaultSeverity() const override { return Severity::Warning; }
		std::vector<std::string> GetDocumentTags() const override { return { "splat" }; }
		void Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const override;
	};
}
