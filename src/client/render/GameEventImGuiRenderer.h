#pragma once
// CMANGOS.31 (Phase 5.31 step 3+4) — GameEventImGuiRenderer : panneau ImGui
// pour la fenetre GameEvents (events saisonniers). Liste les events avec
// leur etat courant (Active/Inactive) + countdown via untilTsMs ;
// boutons Subscribe / Unsubscribe globaux ; toast 5s sur dernier
// changement reçu (push 163).
//
// Pas de HUD overlay : seulement le panel principal.
//
// Lit l'etat d'un GameEventUiPresenter, dispatch les inputs UI
// (RequestList / Subscribe / Unsubscribe) via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class GameEventUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau GameEvents. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::GameEventUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant et propager les inputs UI
	/// vers le presenter.
	class GameEventImGuiRenderer
	{
	public:
		GameEventImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::GameEventUiPresenter* presenter) { m_presenter = presenter; }

		/// Active/desactive le rendu du panel principal.
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Game Events" (si IsEnabled()) et le toast 5s
		/// pour le dernier StateChange reçu (rendu independamment du flag
		/// IsEnabled() puisque les push peuvent arriver panneau ferme).
		/// A appeler entre \c ImGui::NewFrame() et \c ImGui::Render(),
		/// apres NewFrame, si le presenter est valide.
		void Render();

	private:
		/// Dessine le panneau principal (liste events + boutons + countdowns).
		void RenderMainPanel();

		/// Dessine le toast 5s en bas-droite si lastChange* est recent.
		void RenderToast();

		engine::client::GameEventUiPresenter* m_presenter = nullptr;
		bool                                  m_enabled   = false;
		uint32_t                              m_viewportW = 0;
		uint32_t                              m_viewportH = 0;
	};
}
