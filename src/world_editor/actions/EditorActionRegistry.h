#pragma once
// EditorActionRegistry — registre central des actions de l'éditeur monde.
// Rempli au boot (WorldEditorShell::Init pour les actions autonomes du
// shell, WorldEditorImGui::RegisterEditorActions pour les actions session),
// consommé chaque frame par les surfaces UI. Aucune dépendance ImGui.

#include "src/world_editor/actions/EditorAction.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace engine::editor::world::actions
{
	/// Registre ordonné d'`EditorAction`. L'ordre d'enregistrement est
	/// préservé par `Actions()` : les surfaces qui itèrent (menu Outils,
	/// palette de commandes, fenêtre raccourcis) affichent donc les actions
	/// dans l'ordre où elles ont été déclarées.
	///
	/// Contraintes thread/timing : toutes les méthodes doivent être appelées
	/// depuis le main thread (les std::function capturent des états UI/doc
	/// non thread-safe).
	class EditorActionRegistry
	{
	public:
		/// Enregistre une action. Refuse (retour false + LOG_WARN) un id vide
		/// ou déjà présent — garantit l'unicité des ids sur laquelle reposent
		/// `Find` et les surfaces UI.
		/// Effet de bord : prend possession de `action` (move).
		bool Register(EditorAction action);

		/// Vue lecture seule, ordre d'enregistrement préservé.
		const std::vector<EditorAction>& Actions() const { return m_actions; }

		/// Lookup par id. \return nullptr si absent. Le pointeur reste valide
		/// tant qu'aucun `Register`/`Clear` n'intervient (vector interne).
		const EditorAction* Find(std::string_view id) const;

		/// Évalue le prédicat `enabled` de l'action (nul => true).
		static bool IsEnabled(const EditorAction& action);

		/// Nombre d'actions enregistrées.
		size_t Size() const { return m_actions.size(); }

		/// Vide le registre. Réservé aux tests (le runtime enregistre une
		/// seule fois par session).
		void Clear() { m_actions.clear(); }

	private:
		std::vector<EditorAction> m_actions;
	};
}
