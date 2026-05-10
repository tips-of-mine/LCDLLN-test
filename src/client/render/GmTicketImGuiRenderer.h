#pragma once
// CMANGOS.32 (Phase 5.32 step 3+4) — GmTicketImGuiRenderer : panel ImGui pour
// la boite a tickets support GM cote joueur. Lit l'etat d'un GmTicketUiPresenter,
// dispatch les inputs UI vers le presenter via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class GmTicketUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui de la boite a tickets joueur. Pas de logique de fetch /
	/// parse : celle-ci est dans \ref engine::client::GmTicketUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant et propager les inputs UI
	/// vers le presenter.
	class GmTicketImGuiRenderer
	{
	public:
		GmTicketImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::GmTicketUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Support GM" + compose dialog (si \c isComposeOpen).
		/// A appeler entre \c ImGui::NewFrame() et \c ImGui::Render(), apres
		/// NewFrame, si le presenter est valide et IsEnabled().
		void Render();

	private:
		void RenderListPanel();
		void RenderComposeDialog();

		engine::client::GmTicketUiPresenter* m_presenter = nullptr;
		bool                                  m_enabled  = false;
		uint32_t                              m_viewportW = 0;
		uint32_t                              m_viewportH = 0;

		/// Buffer C-string pour le compose. Synchronise depuis le presenter
		/// quand le compose s'ouvre, puis push -> presenter via SetComposeBody.
		char m_bodyBuf[4200]{}; ///< 4096 max + marge NUL/UI.
	};
}
