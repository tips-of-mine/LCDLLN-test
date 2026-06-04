#pragma once

// M100.17 — Commandes de placement (undo/redo via CommandStack).

#include <vector>

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/PlacementDocument.h"

namespace engine::editor::world
{
	/// Pose une ou plusieurs instances (single / drag-line / scatter). Undo les
	/// retire par instanceId. Les instanceId sont assignés AVANT la création de
	/// la commande (via PlacementDocument::AllocInstanceId) pour un undo exact.
	class PlacePropsCommand final : public ICommand
	{
	public:
		PlacePropsCommand(PlacementDocument& doc,
		                  std::vector<engine::world::instances::PropInstance> instances)
			: m_doc(doc), m_instances(std::move(instances)) {}

		const char* GetLabel() const override { return "Place props"; }
		size_t GetMemoryFootprint() const override
		{
			return sizeof(*this) + m_instances.size() * sizeof(engine::world::instances::PropInstance);
		}
		void Execute() override
		{
			for (const auto& p : m_instances) m_doc.Add(p);
		}
		void Undo() override
		{
			for (const auto& p : m_instances) m_doc.RemoveById(p.instanceId);
		}

	private:
		PlacementDocument& m_doc;
		std::vector<engine::world::instances::PropInstance> m_instances;
	};
}
