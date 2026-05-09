// src/world_editor/AddRiverCommand.cpp
#include "src/world_editor/AddRiverCommand.h"

namespace engine::editor::world
{
	size_t AddRiverCommand::GetMemoryFootprint() const
	{
		return sizeof(*this)
			+ m_river.name.size()
			+ m_river.nodes.size() * sizeof(engine::world::water::RiverNode);
	}

	void AddRiverCommand::Execute()
	{
		m_doc->Mutable().rivers.push_back(m_river);
		m_doc->MarkDirty();
	}

	void AddRiverCommand::Undo()
	{
		if (!m_doc->Mutable().rivers.empty())
			m_doc->Mutable().rivers.pop_back();
		m_doc->MarkDirty();
	}
}
