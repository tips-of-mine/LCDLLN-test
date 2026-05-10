#pragma once
// CMANGOS.24 (Phase 3.24 step 3+4) — ReputationImGuiRenderer : panneau ImGui
// pour la liste de reputations cote joueur. Lit l'etat d'un
// ReputationUiPresenter, dispatch les inputs UI vers le presenter via
// accesseurs / methodes.

#include <cstdint>

namespace engine::client { class ReputationUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau Reputation. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::ReputationUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant et propager les inputs UI
	/// vers le presenter.
	class ReputationImGuiRenderer
	{
	public:
		ReputationImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::ReputationUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Reputation" + toast push s'il est actif.
		/// A appeler entre \c ImGui::NewFrame() et \c ImGui::Render(), apres
		/// NewFrame, si le presenter est valide et IsEnabled().
		void Render();

	private:
		void RenderListPanel();
		void RenderToast();

		engine::client::ReputationUiPresenter* m_presenter = nullptr;
		bool                                    m_enabled  = false;
		uint32_t                                m_viewportW = 0;
		uint32_t                                m_viewportH = 0;
	};
}
