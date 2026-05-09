#pragma once
#include "src/world_editor/world/IPanel.h"

namespace engine::editor::world { class CommandStack; }

namespace engine::editor::world::panels
{
	/// Panneau History (M100.2) : liste les commandes empilées dans le
	/// `CommandStack` partagé du shell, avec leur label et empreinte mémoire.
	/// L'item le plus récent est marqué comme "active" (sélectionné).
	/// Cliquer sur une ligne plus ancienne déclenche `RewindTo` en cascade
	/// jusqu'à elle ; cliquer sur "Clear History" vide les deux piles.
	///
	/// Possède un pointeur faible vers le `CommandStack` du shell (durée de
	/// vie : le shell garantit que la pile lui survit).
	///
	/// Contraintes thread/timing : `Render` appelé depuis le main thread,
	/// dans la phase ImGui de `Engine::DrawFrame`.
	class HistoryPanel final : public IPanel
	{
	public:
		/// \param stack Pile undo/redo possédée par `WorldEditorShell`.
		/// Doit rester valide tant que ce panneau existe (le shell détient
		/// les deux et les libère ensemble).
		explicit HistoryPanel(CommandStack* stack) : m_stack(stack) {}

		/// Identifiant stable utilisé comme nom de window ImGui et clé d'ini.
		const char* GetName() const override { return "History"; }

		/// Rend la liste des commandes (label + bytes), un bouton "Clear
		/// History", et déclenche `RewindTo` quand l'utilisateur sélectionne
		/// une ligne plus ancienne. Effet de bord : ImGui state ; éventuel
		/// appel à `m_stack->Clear()` ou `m_stack->RewindTo(i)`.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		CommandStack* m_stack = nullptr;
		bool m_visible = true;
	};
}
