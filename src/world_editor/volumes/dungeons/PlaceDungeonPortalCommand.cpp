#include "src/world_editor/volumes/dungeons/PlaceDungeonPortalCommand.h"

#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"

#include <utility>

namespace engine::editor::world::volumes::dungeons
{
	PlaceDungeonPortalCommand::PlaceDungeonPortalCommand(DungeonPortalDocument& doc,
		DungeonPortalInstance instance)
		: m_doc(&doc)
		, m_instance(std::move(instance))
	{
	}

	size_t PlaceDungeonPortalCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(PlaceDungeonPortalCommand);
		bytes += m_instance.dungeonTemplateId.capacity();
		bytes += m_instance.displayName.capacity();
		bytes += m_instance.decorativeMeshPath.capacity();
		return bytes;
	}

	void PlaceDungeonPortalCommand::Execute()
	{
		if (m_doc == nullptr) return;
		m_insertedGuid = m_doc->Add(m_instance);
	}

	void PlaceDungeonPortalCommand::Undo()
	{
		if (m_doc == nullptr) return;
		if (m_insertedGuid != kInvalidDungeonPortalGuid)
		{
			m_doc->Remove(m_insertedGuid);
			m_insertedGuid = kInvalidDungeonPortalGuid;
		}
	}
}
