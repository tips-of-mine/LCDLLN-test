#include "src/world_editor/scene/EditorSelection.h"

#include <algorithm>

namespace engine::editor::scene
{
	void EditorSelection::Select(EntityId id)
	{
		if (m_set.size() == 1 && m_current == id) return; // déjà la seule sélection
		m_current = id;
		m_set.clear();
		if (id.kind != EntityKind::None) m_set.push_back(id);
		if (m_onChanged) m_onChanged(m_current);
	}

	void EditorSelection::Clear()
	{
		if (m_current.kind == EntityKind::None && m_set.empty()) return; // déjà vide
		m_current = EntityId{};
		m_set.clear();
		if (m_onChanged) m_onChanged(m_current);
	}

	void EditorSelection::SelectMany(const std::vector<EntityId>& ids)
	{
		m_set = ids;
		m_current = ids.empty() ? EntityId{} : ids.front();
		if (m_onChanged) m_onChanged(m_current);
	}

	void EditorSelection::ToggleInSelection(EntityId id)
	{
		auto it = std::find(m_set.begin(), m_set.end(), id);
		if (it != m_set.end())
			m_set.erase(it);
		else
			m_set.push_back(id);
		m_current = m_set.empty() ? EntityId{} : m_set.front();
		if (m_onChanged) m_onChanged(m_current);
	}

	bool EditorSelection::IsSelected(EntityId id) const
	{
		return std::find(m_set.begin(), m_set.end(), id) != m_set.end();
	}
}
