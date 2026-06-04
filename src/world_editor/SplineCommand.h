#pragma once

// M100.29 — Commande d'ajout de spline (undo/redo). Header-only. La peinture
// splat associée est déclenchée séparément (différée).

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/SplineDocument.h"

namespace engine::editor::world
{
	class AddSplineCommand final : public ICommand
	{
	public:
		AddSplineCommand(SplineDocument& doc, engine::world::spline::Spline spline)
			: m_doc(doc), m_spline(std::move(spline)) {}

		const char* GetLabel() const override { return "Add spline"; }
		size_t GetMemoryFootprint() const override
		{
			return sizeof(*this) + m_spline.nodes.size() * sizeof(engine::world::spline::SplineNode);
		}
		void Execute() override { m_doc.Add(m_spline); }
		void Undo() override { m_doc.RemoveLast(); }

	private:
		SplineDocument& m_doc;
		engine::world::spline::Spline m_spline;
	};
}
