// EditorActionRegistry — implémentation. Voir EditorActionRegistry.h.

#include "src/world_editor/actions/EditorActionRegistry.h"

#include "src/shared/core/Log.h"

namespace engine::editor::world::actions
{
	bool EditorActionRegistry::Register(EditorAction action)
	{
		if (action.id.empty())
		{
			LOG_WARN(EditorWorld, "EditorActionRegistry: id vide refuse (label='{}')",
				action.label);
			return false;
		}
		if (Find(action.id) != nullptr)
		{
			LOG_WARN(EditorWorld, "EditorActionRegistry: id duplique refuse '{}'",
				action.id);
			return false;
		}
		m_actions.push_back(std::move(action));
		return true;
	}

	const EditorAction* EditorActionRegistry::Find(std::string_view id) const
	{
		// Lookup linéaire assumé : le registre reste petit (< 100 actions) et
		// `Find` n'est appelé que sur des chemins UI (pas de hot loop).
		for (const EditorAction& a : m_actions)
		{
			if (a.id == id)
			{
				return &a;
			}
		}
		return nullptr;
	}

	bool EditorActionRegistry::IsEnabled(const EditorAction& action)
	{
		return action.enabled ? action.enabled() : true;
	}
}
