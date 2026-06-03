#include "src/world_editor/scene/EditorSelection.h"

namespace engine::editor::scene
{
	void EditorSelection::Select(EntityId id)
	{
		if (id == m_current) return; // déjà sélectionné : pas de notification
		m_current = id;
		if (m_onChanged) m_onChanged(m_current);
	}

	void EditorSelection::Clear()
	{
		if (m_current.kind == EntityKind::None) return; // déjà vide
		m_current = EntityId{};
		if (m_onChanged) m_onChanged(m_current);
	}
}
