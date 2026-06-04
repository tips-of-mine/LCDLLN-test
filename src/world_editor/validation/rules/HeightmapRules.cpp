// M100.48 — Implémentation des règles heightmap.

#include "src/world_editor/validation/rules/HeightmapRules.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <cmath>

namespace engine::editor::world::validation
{
	void HeightmapHolesRule::Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const
	{
		for (const auto& entry : ctx.terrainChunks)
		{
			if (!entry.chunk) continue;
			const auto& c = *entry.chunk;
			for (size_t i = 0; i < c.heights.size(); ++i)
			{
				const float h = c.heights[i];
				if (std::isnan(h) || std::isinf(h))
				{
					const uint32_t x = static_cast<uint32_t>(i % c.resolutionX);
					const uint32_t z = static_cast<uint32_t>(i / c.resolutionX);
					ValidationIssue issue;
					issue.ruleId = GetRuleId();
					issue.title = "Trou dans la heightmap";
					issue.description = "Cellule (" + std::to_string(x) + ", " + std::to_string(z) + ") NaN/infinie.";
					issue.severity = Severity::Error;
					issue.worldPosition = {
						entry.originWorld.x + static_cast<float>(x) * c.cellSizeMeters,
						0.0f,
						entry.originWorld.z + static_cast<float>(z) * c.cellSizeMeters
					};
					issuesOut.push_back(std::move(issue));
				}
			}
		}
	}

	void HeightmapExtremeSlopeRule::Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const
	{
		// Pente entre une cellule et son voisin +X / +Z. tan(85°) ≈ 11.43.
		constexpr float kTan85 = 11.4300523f;
		for (const auto& entry : ctx.terrainChunks)
		{
			if (!entry.chunk) continue;
			const auto& c = *entry.chunk;
			if (c.cellSizeMeters <= 0.0f) continue;
			for (uint32_t z = 0; z < c.resolutionZ; ++z)
			{
				for (uint32_t x = 0; x < c.resolutionX; ++x)
				{
					const float h = c.heights[z * c.resolutionX + x];
					if (std::isnan(h) || std::isinf(h)) continue; // géré par holes.
					float maxDelta = 0.0f;
					if (x + 1 < c.resolutionX)
						maxDelta = std::fmax(maxDelta, std::fabs(c.heights[z * c.resolutionX + (x + 1)] - h));
					if (z + 1 < c.resolutionZ)
						maxDelta = std::fmax(maxDelta, std::fabs(c.heights[(z + 1) * c.resolutionX + x] - h));
					const float slopeTan = maxDelta / c.cellSizeMeters;
					if (slopeTan > kTan85)
					{
						ValidationIssue issue;
						issue.ruleId = GetRuleId();
						issue.title = "Pente extrême";
						issue.description = "Cellule (" + std::to_string(x) + ", " + std::to_string(z) +
							") pente quasi verticale (Δh=" + std::to_string(maxDelta) + " m).";
						issue.severity = Severity::Warning;
						issue.worldPosition = {
							entry.originWorld.x + static_cast<float>(x) * c.cellSizeMeters,
							h,
							entry.originWorld.z + static_cast<float>(z) * c.cellSizeMeters
						};
						issue.suggestedFix = "Adoucir la pente ou poser un mesh insert (overhang/falaise).";
						issuesOut.push_back(std::move(issue));
					}
				}
			}
		}
	}
}
