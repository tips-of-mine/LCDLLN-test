#pragma once

// Auberge éditable (T2) — Déplacement réversible d'un groupe d'instances
// (translation appliquée à tous les membres du groupId). Opère sur le vecteur
// du PlacementDocument (référence non-owning).

#include "src/shared/math/Math.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/PlacementDocument.h"

namespace engine::editor::world
{
	/// Translate toutes les instances du groupe `groupId` de `delta` (m).
	class MoveGroupCommand final : public ICommand
	{
	public:
		MoveGroupCommand(PlacementDocument& doc, uint32_t groupId,
			engine::math::Vec3 delta)
			: m_doc(doc), m_groupId(groupId), m_delta(delta) {}

		const char* GetLabel() const override { return "Move group"; }
		size_t GetMemoryFootprint() const override { return sizeof(*this); }

		void Execute() override { Apply(m_delta); }
		void Undo() override
		{
			Apply(engine::math::Vec3(-m_delta.x, -m_delta.y, -m_delta.z));
		}

	private:
		void Apply(const engine::math::Vec3& d)
		{
			for (auto& p : m_doc.Mutable())
			{
				if (p.groupId != m_groupId) continue;
				p.position.x += d.x; p.position.y += d.y; p.position.z += d.z;
			}
		}
		PlacementDocument& m_doc;
		uint32_t m_groupId;
		engine::math::Vec3 m_delta;
	};
}
