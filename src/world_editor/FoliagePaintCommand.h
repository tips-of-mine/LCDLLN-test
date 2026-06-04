#pragma once

// M100.18 — Commande de peinture foliage (undo/redo). Un stroke = un batch
// d'instances ajoutées ; l'undo retire ce batch (LIFO, cohérent avec la pile).

#include <vector>

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/FoliageDocument.h"

namespace engine::editor::world
{
	class FoliagePaintCommand final : public ICommand
	{
	public:
		FoliagePaintCommand(FoliageDocument& doc,
		                    std::vector<engine::world::foliage::FoliageInstance> batch)
			: m_doc(doc), m_batch(std::move(batch)) {}

		const char* GetLabel() const override { return "Paint foliage"; }
		size_t GetMemoryFootprint() const override
		{
			return sizeof(*this) + m_batch.size() * sizeof(engine::world::foliage::FoliageInstance);
		}
		void Execute() override { m_doc.Append(m_batch); }
		void Undo() override { m_doc.RemoveLast(m_batch.size()); }

	private:
		FoliageDocument& m_doc;
		std::vector<engine::world::foliage::FoliageInstance> m_batch;
	};
}
