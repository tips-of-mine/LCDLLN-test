// engine/editor/world/AddRiverCommand.h
#pragma once

#include "engine/editor/world/CommandStack.h"
#include "engine/editor/world/WaterDocument.h"
#include "engine/world/water/WaterSurfaces.h"

namespace engine::editor::world
{
	/// Commande undoable : ajoute une `RiverInstance` au WaterDocument (M100.13).
	/// Execute → push_back ; Undo → pop_back. Marque le doc dirty à chaque
	/// transition (sauve disque déclenché au shutdown ou Save Zone).
	class AddRiverCommand : public ICommand
	{
	public:
		AddRiverCommand(WaterDocument& doc, engine::world::water::RiverInstance river) noexcept
			: m_doc(&doc), m_river(std::move(river)) {}

		const char* GetLabel() const override { return "Add River"; }
		size_t      GetMemoryFootprint() const override;
		void        Execute() override;
		void        Undo() override;

	private:
		WaterDocument*                          m_doc;
		engine::world::water::RiverInstance     m_river;
	};
}
