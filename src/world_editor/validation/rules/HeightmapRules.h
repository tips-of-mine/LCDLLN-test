#pragma once

// M100.48 — Règles de validation heightmap.

#include "src/world_editor/validation/IValidationRule.h"

namespace engine::editor::world::validation
{
	/// `heightmap.holes` (Error) : détecte les cellules avec hauteur NaN/±inf.
	class HeightmapHolesRule : public IValidationRule
	{
	public:
		const char* GetRuleId() const override { return "heightmap.holes"; }
		const char* GetCategory() const override { return "heightmap"; }
		const char* GetDescription() const override { return "Cellules de heightmap NaN ou infinies (corruption)."; }
		Severity GetDefaultSeverity() const override { return Severity::Error; }
		std::vector<std::string> GetDocumentTags() const override { return { "terrain" }; }
		void Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const override;
	};

	/// `heightmap.extreme_slope` (Warning) : cellules dont la pente locale
	/// dépasse 85° (quasi verticale ; la heightmap 2D gère mal ce cas).
	class HeightmapExtremeSlopeRule : public IValidationRule
	{
	public:
		const char* GetRuleId() const override { return "heightmap.extreme_slope"; }
		const char* GetCategory() const override { return "heightmap"; }
		const char* GetDescription() const override { return "Pente locale > 85° (quasi verticale)."; }
		Severity GetDefaultSeverity() const override { return Severity::Warning; }
		std::vector<std::string> GetDocumentTags() const override { return { "terrain" }; }
		void Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const override;
	};
}
