// engine/editor/world/AddLakeCommand.cpp
#include "engine/editor/world/AddLakeCommand.h"

namespace engine::editor::world
{
	size_t AddLakeCommand::GetMemoryFootprint() const
	{
		// Approximation : struct + name + polygon vertices.
		return sizeof(*this)
			+ m_lake.name.size()
			+ m_lake.polygon.size() * sizeof(engine::math::Vec3);
	}

	void AddLakeCommand::Execute()
	{
		m_doc->Mutable().lakes.push_back(m_lake);
		m_doc->MarkDirty();
	}

	void AddLakeCommand::Undo()
	{
		// Précondition (cf. ICommand) : Execute a été appelé en dernier ;
		// le lake en sommet est notre lake.
		if (!m_doc->Mutable().lakes.empty())
			m_doc->Mutable().lakes.pop_back();
		m_doc->MarkDirty();
	}
}
