#pragma once

#include "src/world_editor/modes/EditorMode.h"

#include <cstddef>
#include <functional>
#include <unordered_map>

namespace engine::editor::world::modes
{
	/// Centralise l'état du mode éditeur courant (M100.45, Phase 12).
	/// Singleton accessible depuis tous les panels.
	///
	/// `SetCurrentMode` persiste immédiatement via `UserPrefsStore` puis
	/// notifie tous les abonnés de manière synchrone — un `ToolPropertiesPanel`
	/// ouvert peut ainsi se re-render dès le basculement du toggle Options.
	///
	/// Persistance : la valeur vit dans `user_prefs.json` (clé `editorMode`).
	/// Au boot, `WorldEditorShell` appelle `SetCurrentModeSilent` avec la
	/// valeur chargée par `UserPrefsStore` pour aligner le registry sans
	/// ré-écrire le fichier.
	class EditorModeRegistry
	{
	public:
		static EditorModeRegistry& Instance();

		EditorMode GetCurrentMode() const { return m_mode; }

		/// Change le mode : persiste dans `user_prefs.json` puis notifie les
		/// abonnés. No-op si le mode est déjà la valeur demandée.
		void SetCurrentMode(EditorMode mode);

		/// Change le mode sans persister (utilisé au boot pour aligner le
		/// registry sur la valeur déjà chargée depuis le disque). Notifie
		/// quand même les abonnés.
		void SetCurrentModeSilent(EditorMode mode);

		/// Abonnement aux changements de mode. Le callback est invoqué
		/// **synchrone** depuis `SetCurrentMode` / `SetCurrentModeSilent`.
		/// \return un handle opaque à passer à `Unsubscribe`.
		using ChangeCallback = std::function<void(EditorMode)>;
		size_t Subscribe(ChangeCallback cb);
		void   Unsubscribe(size_t handle);

		/// Réinitialise l'état (mode Simple, aucun abonné). Réservé aux
		/// tests — le singleton est sinon persistant pour la session.
		void ResetForTesting();

	private:
		EditorModeRegistry() = default;

		void NotifySubscribers();

		EditorMode m_mode = EditorMode::Simple;
		std::unordered_map<size_t, ChangeCallback> m_callbacks;
		size_t m_nextHandle = 1u;
	};
}
