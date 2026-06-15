#pragma once
// SP-D — Arbre de compétences par-classe — renderer ImGui.
// Lit ClassSkillTreeUiPresenter ; affiche 3 colonnes (single / aoe / def) × paliers.
// Aucun fetch/parse (presenter). Stub no-op hors Windows.

#include <cstdint>

namespace engine::client { class ClassSkillTreeUiPresenter; }

namespace engine::render
{
	class ClassSkillTreeImGuiRenderer
	{
	public:
		ClassSkillTreeImGuiRenderer() = default;

		/// Enregistre le presenter dont l'état sera lu à chaque Render().
		void SetPresenter(engine::client::ClassSkillTreeUiPresenter* presenter) { m_presenter = presenter; }

		/// Active ou désactive l'affichage de la fenêtre.
		void SetEnabled(bool on) { m_enabled = on; }

		/// \return true si la fenêtre est actuellement active.
		bool IsEnabled() const { return m_enabled; }

		/// Met à jour la taille du viewport pour centrer la fenêtre.
		/// \param w Largeur du viewport en pixels.
		/// \param h Hauteur du viewport en pixels.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// À appeler entre ImGui::NewFrame() et ImGui::Render() si presenter valide.
		/// No-op si m_enabled == false ou presenter non initialisé.
		void Render();

	private:
		engine::client::ClassSkillTreeUiPresenter* m_presenter = nullptr;
		bool     m_enabled   = false;
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;
	};
}
