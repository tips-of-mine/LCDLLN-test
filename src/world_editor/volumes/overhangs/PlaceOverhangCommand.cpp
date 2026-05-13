#include "src/world_editor/volumes/overhangs/PlaceOverhangCommand.h"

#include "src/world_editor/volumes/MeshInsertDocument.h"

#include <utility>

namespace engine::editor::world::volumes::overhangs
{
	PlaceOverhangCommand::PlaceOverhangCommand(MeshInsertDocument& meshDoc,
		MeshInsertInstance instance)
		: m_meshDoc(&meshDoc)
		, m_instance(std::move(instance))
	{
	}

	size_t PlaceOverhangCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(PlaceOverhangCommand);
		bytes += m_instance.gltfRelativePath.capacity();
		bytes += m_instance.insertCategory.capacity();
		bytes += m_instance.displayName.capacity();
		return bytes;
	}

	void PlaceOverhangCommand::Execute()
	{
		if (m_meshDoc == nullptr) return;
		m_insertedGuid = m_meshDoc->Add(m_instance);
	}

	void PlaceOverhangCommand::Undo()
	{
		if (m_meshDoc == nullptr) return;
		if (m_insertedGuid != kInvalidMeshInsertGuid)
		{
			m_meshDoc->Remove(m_insertedGuid);
			m_insertedGuid = kInvalidMeshInsertGuid;
		}
	}
}
