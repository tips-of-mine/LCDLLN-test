#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"

#include <cstddef>
#include <cstdint>

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::arches
{
	/// Commande "Place Arch" (M100.42). Insertion pure d'une
	/// `MeshInsertInstance` (`insertCategory = "arch"`). Pas de patch
	/// splat : une arche naturelle est posée au-dessus du sol (rivière,
	/// canyon) et ne modifie pas la heightmap ou le splat sous elle.
	///
	/// Undo strict : retire l'instance par guid.
	class PlaceArchCommand final : public ICommand
	{
	public:
		PlaceArchCommand(MeshInsertDocument& meshDoc, MeshInsertInstance instance);

		const char* GetLabel()           const override { return "Place Arch"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

	private:
		MeshInsertDocument*  m_meshDoc = nullptr;
		MeshInsertInstance   m_instance;
		uint64_t             m_insertedGuid = 0u;
	};
}
