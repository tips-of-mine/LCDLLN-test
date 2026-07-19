#include "src/world_editor/scene/EditorSelection.h"

#include <algorithm>

namespace engine::editor::scene
{
	void EditorSelection::Select(EntityId id)
	{
		if (id.kind == EntityKind::None) { Clear(); return; }
		if (m_items.size() == 1u && m_items.front() == id) return; // déjà exactement {id}
		m_items.clear();
		m_items.push_back(id);
		Notify();
	}

	void EditorSelection::Toggle(EntityId id)
	{
		if (id.kind == EntityKind::None) return;
		const auto it = std::find(m_items.begin(), m_items.end(), id);
		if (it != m_items.end())
		{
			m_items.erase(it);
		}
		else
		{
			m_items.push_back(id); // devient la primaire (back)
		}
		Notify();
	}

	void EditorSelection::Clear()
	{
		if (m_items.empty()) return; // déjà vide
		m_items.clear();
		Notify();
	}

	bool EditorSelection::Contains(EntityId id) const
	{
		return std::find(m_items.begin(), m_items.end(), id) != m_items.end();
	}
}
