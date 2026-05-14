#include "src/world_editor/modes/EditorModeRegistry.h"

#include "src/world_editor/prefs/UserPrefsStore.h"

namespace engine::editor::world::modes
{
	EditorModeRegistry& EditorModeRegistry::Instance()
	{
		static EditorModeRegistry instance;
		return instance;
	}

	void EditorModeRegistry::SetCurrentMode(EditorMode mode)
	{
		if (m_mode == mode) return;
		m_mode = mode;
		// Persistance immédiate (cf. spec : « persists immediatement »).
		// Si le store n'est pas initialisé (tests unitaires hors boot),
		// SetEditorMode est un no-op sûr côté disque.
		prefs::UserPrefsStore::Instance().SetEditorMode(mode);
		NotifySubscribers();
	}

	void EditorModeRegistry::SetCurrentModeSilent(EditorMode mode)
	{
		m_mode = mode;
		NotifySubscribers();
	}

	size_t EditorModeRegistry::Subscribe(ChangeCallback cb)
	{
		const size_t handle = m_nextHandle++;
		m_callbacks.emplace(handle, std::move(cb));
		return handle;
	}

	void EditorModeRegistry::Unsubscribe(size_t handle)
	{
		m_callbacks.erase(handle);
	}

	void EditorModeRegistry::NotifySubscribers()
	{
		// Copie des callbacks : un subscriber pourrait se désabonner depuis
		// son propre callback (modif de m_callbacks pendant l'itération).
		const auto snapshot = m_callbacks;
		for (const auto& kv : snapshot)
		{
			if (kv.second) kv.second(m_mode);
		}
	}

	void EditorModeRegistry::ResetForTesting()
	{
		m_mode = EditorMode::Simple;
		m_callbacks.clear();
		m_nextHandle = 1u;
	}
}
