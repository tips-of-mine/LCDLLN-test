// M100.48 — Implémentation des règles mesh inserts.

#include "src/world_editor/validation/rules/MeshInsertRules.h"

#include "src/world_editor/volumes/MeshInsertInstance.h"

#include <unordered_map>

namespace engine::editor::world::validation
{
	void MeshInsertGltfMissingRule::Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const
	{
		if (!ctx.meshInserts) return;
		for (const auto& mi : *ctx.meshInserts)
		{
			if (mi.gltfRelativePath.empty())
			{
				ValidationIssue issue;
				issue.ruleId = GetRuleId();
				issue.title = "Mesh insert sans asset glTF";
				issue.description = "Instance guid=" + std::to_string(mi.guid) +
					" (cat. " + mi.insertCategory + ") a un chemin glTF vide.";
				issue.severity = Severity::Error;
				issue.worldPosition = mi.worldPosition;
				issue.targetGuid = mi.guid;
				issuesOut.push_back(std::move(issue));
			}
		}
	}

	void MeshInsertDuplicateGuidRule::Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const
	{
		if (!ctx.meshInserts) return;
		std::unordered_map<uint64_t, uint32_t> counts;
		for (const auto& mi : *ctx.meshInserts)
			++counts[mi.guid];

		// Émet un problème par guid dupliqué (au premier doublon rencontré).
		std::unordered_map<uint64_t, bool> reported;
		for (const auto& mi : *ctx.meshInserts)
		{
			if (counts[mi.guid] > 1u && !reported[mi.guid])
			{
				reported[mi.guid] = true;
				ValidationIssue issue;
				issue.ruleId = GetRuleId();
				issue.title = "Guid mesh insert dupliqué";
				issue.description = "Le guid " + std::to_string(mi.guid) + " est utilisé par " +
					std::to_string(counts[mi.guid]) + " instances.";
				issue.severity = Severity::Error;
				issue.worldPosition = mi.worldPosition;
				issue.targetGuid = mi.guid;
				issue.suggestedFix = "Régénérer un guid unique pour les doublons.";
				issuesOut.push_back(std::move(issue));
			}
		}
	}
}
