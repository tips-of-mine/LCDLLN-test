#pragma once

#include "src/world_editor/SelectionTool.h"           // SelectionRect, SelectablePoint
#include "src/world_editor/scene/EditorSelection.h"    // EntityId, EntityKind

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace engine::editor::scene
{
	/// Candidat sélectionnable projeté au sol (X/Z monde).
	struct SelectableEntity
	{
		EntityId id{};
		float    x = 0.0f;
		float    z = 0.0f;
	};

	/// Encode `(kind, index)` sur 32 bits (8 bits kind << 24 | index 24 bits)
	/// pour transiter par l'API uint32 de SelectionTool.
	uint32_t EncodeEntityId(EntityId id);
	EntityId DecodeEntityId(uint32_t encoded);

	/// Entité la plus proche de (x,z) dont la distance ≤ `radius` (monde).
	/// std::nullopt si aucune dans le rayon.
	std::optional<EntityId> PickNearest(const std::vector<SelectableEntity>& pts,
		float x, float z, float radius);

	/// Entités dont la projection X/Z tombe dans `rect` (réutilise SelectInRect).
	std::vector<EntityId> PickInRect(const std::vector<SelectableEntity>& pts,
		engine::editor::world::SelectionRect rect);

	/// Entités dont la projection X/Z tombe dans le polygone lasso (SelectInLasso).
	std::vector<EntityId> PickInLasso(const std::vector<SelectableEntity>& pts,
		const std::vector<std::pair<float, float>>& polygon);
}
