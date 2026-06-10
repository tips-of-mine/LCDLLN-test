#include "src/world_editor/scene/DeleteEntitiesCommand.h"

namespace engine::editor::scene
{
	void DeleteEntitiesCommand::Execute()
	{
		if (m_remove) m_snapshot = m_remove(m_ids);
	}

	void DeleteEntitiesCommand::Undo()
	{
		if (m_restore) m_restore(m_snapshot);
	}

	size_t DeleteEntitiesCommand::GetMemoryFootprint() const
	{
		// sizeof n'évalue pas l'opérande : on utilise les types d'élément
		// explicitement pour rester valide même si les vecteurs sont vides.
		return sizeof(DeleteEntitiesCommand)
			+ m_ids.capacity() * sizeof(EntityId)
			+ m_snapshot.layout.capacity()
				* sizeof(std::pair<uint32_t, engine::editor::WorldMapEditLayoutInstance>)
			+ m_snapshot.mesh.capacity()
				* sizeof(std::pair<uint32_t, engine::editor::world::volumes::MeshInsertInstance>)
			+ m_snapshot.dungeon.capacity()
				* sizeof(std::pair<uint32_t, engine::editor::world::volumes::dungeons::DungeonPortalInstance>);
	}
}
