#include "src/world_editor/scene/SelectionQuery.h"

namespace engine::editor::scene
{
	using engine::editor::world::SelectablePoint;
	using engine::editor::world::SelectionRect;

	uint32_t EncodeEntityId(EntityId id)
	{
		return (static_cast<uint32_t>(id.kind) << 24) | (id.index & 0x00FFFFFFu);
	}

	EntityId DecodeEntityId(uint32_t encoded)
	{
		EntityId id;
		id.kind  = static_cast<EntityKind>((encoded >> 24) & 0xFFu);
		id.index = encoded & 0x00FFFFFFu;
		return id;
	}

	std::optional<EntityId> PickNearest(const std::vector<SelectableEntity>& pts,
		float x, float z, float radius)
	{
		const float r2 = radius * radius;
		std::optional<EntityId> best;
		float bestD2 = r2;
		for (const auto& p : pts)
		{
			const float dx = p.x - x;
			const float dz = p.z - z;
			const float d2 = dx * dx + dz * dz;
			if (d2 <= bestD2)
			{
				bestD2 = d2;
				best = p.id;
			}
		}
		return best;
	}

	std::vector<EntityId> PickInRect(const std::vector<SelectableEntity>& pts,
		SelectionRect rect)
	{
		std::vector<SelectablePoint> raw;
		raw.reserve(pts.size());
		for (const auto& p : pts)
			raw.push_back(SelectablePoint{ EncodeEntityId(p.id), p.x, p.z });

		std::vector<uint32_t> ids = engine::editor::world::SelectInRect(raw, rect);
		std::vector<EntityId> out;
		out.reserve(ids.size());
		for (uint32_t e : ids) out.push_back(DecodeEntityId(e));
		return out;
	}

	std::vector<EntityId> PickInLasso(const std::vector<SelectableEntity>& pts,
		const std::vector<std::pair<float, float>>& polygon)
	{
		std::vector<SelectablePoint> raw;
		raw.reserve(pts.size());
		for (const auto& p : pts)
			raw.push_back(SelectablePoint{ EncodeEntityId(p.id), p.x, p.z });

		std::vector<uint32_t> ids = engine::editor::world::SelectInLasso(raw, polygon);
		std::vector<EntityId> out;
		out.reserve(ids.size());
		for (uint32_t e : ids) out.push_back(DecodeEntityId(e));
		return out;
	}
}
