#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstddef>
#include <cstdint>

namespace engine::editor::world::volumes::dungeons
{
	class DungeonPortalDocument;

	/// Commande "Place Dungeon Portal" (M100.43). Insertion pure d'une
	/// `DungeonPortalInstance` dans `DungeonPortalDocument`. Pas de patch
	/// terrain (un portail est posé tel quel — l'éventuel mesh décoratif
	/// associé n'altère pas la heightmap).
	///
	/// Undo strict : retire par guid.
	class PlaceDungeonPortalCommand final : public ICommand
	{
	public:
		PlaceDungeonPortalCommand(DungeonPortalDocument& doc, DungeonPortalInstance instance);

		const char* GetLabel()           const override { return "Place Dungeon Portal"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

	private:
		DungeonPortalDocument*  m_doc = nullptr;
		DungeonPortalInstance   m_instance;
		uint64_t                m_insertedGuid = 0u;
	};
}
