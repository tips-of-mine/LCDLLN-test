// src/world_editor/water/AddLakeCommand.h
#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/water/WaterDocument.h"
#include "src/client/world/water/WaterSurfaces.h"

namespace engine::editor::world
{
	/// Commande undoable : ajoute une `LakeInstance` au WaterDocument (M100.13).
	/// Execute → push_back ; Undo → pop_back. Marque le doc dirty à chaque
	/// transition (sauve disque déclenché au shutdown ou Save Zone).
	class AddLakeCommand : public ICommand
	{
	public:
		AddLakeCommand(WaterDocument& doc, engine::world::water::LakeInstance lake) noexcept
			: m_doc(&doc), m_lake(std::move(lake)) {}

		const char* GetLabel() const override { return "Add Lake"; }
		size_t      GetMemoryFootprint() const override;
		void        Execute() override;
		void        Undo() override;

	private:
		WaterDocument*                          m_doc;
		engine::world::water::LakeInstance      m_lake;
	};
}
