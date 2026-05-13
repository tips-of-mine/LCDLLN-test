#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"

#include <cstddef>
#include <cstdint>

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::overhangs
{
	/// Commande "Place Overhang" (M100.41). Insertion pure d'une
	/// `MeshInsertInstance` (`insertCategory = "overhang"`) dans le
	/// `MeshInsertDocument`. Pas de patch splat : un overhang est
	/// adossé à une falaise existante (qui porte déjà sa couche
	/// rocheuse), donc inutile de camoufler quoi que ce soit au sol.
	///
	/// Undo strict : retire l'instance par guid.
	class PlaceOverhangCommand final : public ICommand
	{
	public:
		PlaceOverhangCommand(MeshInsertDocument& meshDoc, MeshInsertInstance instance);

		const char* GetLabel()           const override { return "Place Overhang"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

	private:
		MeshInsertDocument*  m_meshDoc = nullptr;
		MeshInsertInstance   m_instance;
		uint64_t             m_insertedGuid = 0u;
	};
}
