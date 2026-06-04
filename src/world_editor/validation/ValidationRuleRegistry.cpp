// M100.48 — Implémentation ValidationRuleRegistry + enregistrement MVP.

#include "src/world_editor/validation/ValidationRuleRegistry.h"

#include "src/world_editor/validation/rules/HeightmapRules.h"
#include "src/world_editor/validation/rules/MeshInsertRules.h"
#include "src/world_editor/validation/rules/SplatRules.h"

namespace engine::editor::world::validation
{
	void ValidationRuleRegistry::RegisterRule(std::unique_ptr<IValidationRule> rule)
	{
		if (!rule) return;
		m_rulesView.push_back(rule.get());
		m_rules.push_back(std::move(rule));
	}

	std::vector<IValidationRule*> ValidationRuleRegistry::GetRulesByCategory(const std::string& category) const
	{
		std::vector<IValidationRule*> out;
		for (IValidationRule* r : m_rulesView)
		{
			if (category == r->GetCategory())
				out.push_back(r);
		}
		return out;
	}

	void RegisterMvpValidationRules(ValidationRuleRegistry& registry)
	{
		// Heightmap.
		registry.RegisterRule(std::make_unique<HeightmapHolesRule>());
		registry.RegisterRule(std::make_unique<HeightmapExtremeSlopeRule>());
		// Splat.
		registry.RegisterRule(std::make_unique<SplatSumInvalidRule>());
		registry.RegisterRule(std::make_unique<SplatEmptyCellRule>());
		// Mesh inserts.
		registry.RegisterRule(std::make_unique<MeshInsertGltfMissingRule>());
		registry.RegisterRule(std::make_unique<MeshInsertDuplicateGuidRule>());
	}
}
