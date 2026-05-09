// src/world_editor/AddRiverCommand.h
#pragma once

#include "src/world_editor/CommandStack.h"
#include "src/world_editor/WaterDocument.h"
#include "src/client/world/water/WaterSurfaces.h"

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
