// M100.48 — Implémentation des règles splat.

#include "src/world_editor/validation/rules/SplatRules.h"

#include "src/client/world/terrain/SplatMap.h"

#include <string>

namespace engine::editor::world::validation
{
	namespace
	{
		/// Nombre max de problèmes émis par règle (évite un rapport pléthorique).
		/// Si dépassé, un problème de synthèse est ajouté (pas de troncature
		/// silencieuse).
		constexpr uint32_t kMaxIssuesPerRule = 32u;
	}

	void SplatSumInvalidRule::Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const
	{
		if (!ctx.splat) return;
		const auto& s = *ctx.splat;
		if (s.layerCount == 0) return;
		const uint32_t res = s.resolution;
		uint32_t emitted = 0;
		uint32_t totalViolations = 0;
		for (uint32_t z = 0; z < res; ++z)
		{
			for (uint32_t x = 0; x < res; ++x)
			{
				const size_t base = (static_cast<size_t>(z) * res + x) * s.layerCount;
				if (base + s.layerCount > s.weights.size()) return; // buffer tronqué.
				uint32_t sum = 0;
				for (uint32_t l = 0; l < s.layerCount; ++l)
					sum += s.weights[base + l];
				if (sum == 0) continue; // cellule vide → règle splat.empty_cell.
				// Tolérance ±2 pour l'arrondi entier.
				if (sum < 253u || sum > 257u)
				{
					++totalViolations;
					if (emitted < kMaxIssuesPerRule)
					{
						ValidationIssue issue;
						issue.ruleId = GetRuleId();
						issue.title = "Somme splat invalide";
						issue.description = "Cellule (" + std::to_string(x) + ", " + std::to_string(z) +
							") somme=" + std::to_string(sum) + " (attendu 255).";
						issue.severity = Severity::Error;
						issue.worldPosition = ctx.splatOriginWorld;
						issuesOut.push_back(std::move(issue));
						++emitted;
					}
				}
			}
		}
		if (totalViolations > emitted)
		{
			ValidationIssue summary;
			summary.ruleId = GetRuleId();
			summary.title = "Somme splat invalide (synthèse)";
			summary.description = std::to_string(totalViolations) + " cellules au total ; " +
				std::to_string(emitted) + " listées.";
			summary.severity = Severity::Error;
			summary.worldPosition = ctx.splatOriginWorld;
			issuesOut.push_back(std::move(summary));
		}
	}

	void SplatEmptyCellRule::Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const
	{
		if (!ctx.splat) return;
		const auto& s = *ctx.splat;
		if (s.layerCount == 0) return;
		const uint32_t res = s.resolution;
		uint32_t emitted = 0;
		uint32_t totalEmpty = 0;
		for (uint32_t z = 0; z < res; ++z)
		{
			for (uint32_t x = 0; x < res; ++x)
			{
				const size_t base = (static_cast<size_t>(z) * res + x) * s.layerCount;
				if (base + s.layerCount > s.weights.size()) return;
				uint32_t sum = 0;
				for (uint32_t l = 0; l < s.layerCount; ++l)
					sum += s.weights[base + l];
				if (sum == 0)
				{
					++totalEmpty;
					if (emitted < kMaxIssuesPerRule)
					{
						ValidationIssue issue;
						issue.ruleId = GetRuleId();
						issue.title = "Cellule splat vide";
						issue.description = "Cellule (" + std::to_string(x) + ", " + std::to_string(z) +
							") sans couche peinte.";
						issue.severity = Severity::Warning;
						issue.worldPosition = ctx.splatOriginWorld;
						issuesOut.push_back(std::move(issue));
						++emitted;
					}
				}
			}
		}
		if (totalEmpty > emitted)
		{
			ValidationIssue summary;
			summary.ruleId = GetRuleId();
			summary.title = "Cellules splat vides (synthèse)";
			summary.description = std::to_string(totalEmpty) + " cellules vides ; " +
				std::to_string(emitted) + " listées.";
			summary.severity = Severity::Warning;
			summary.worldPosition = ctx.splatOriginWorld;
			issuesOut.push_back(std::move(summary));
		}
	}
}
