#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4) — ArenaImGuiRenderer : panneau ImGui pour
// la fenetre Arena cote joueur. Lit l'etat d'un ArenaUiPresenter, dispatch
// les inputs UI (RequestTeams / Queue / LeaveQueue / Accept / Decline) via
// accesseurs / methodes.

#include <cstdint>

namespace engine::client { class ArenaUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau Arena. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::ArenaUiPresenter. Le renderer ne
	/// fait que dessiner l'etat courant et propager les inputs UI vers le
	/// presenter.
	class ArenaImGuiRenderer
	{
	public:
		ArenaImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::ArenaUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Arena" + popup proposal s'il est actif. A appeler
		/// entre \c ImGui::NewFrame() et \c ImGui::Render(), apres NewFrame,
		/// si le presenter est valide et IsEnabled().
		void Render();

	private:
		/// Dessine le panneau principal (liste teams + queue/leave + last result).
		void RenderMainPanel();

		/// Dessine le popup quand un proposal est en attente.
		void RenderProposalPopup();

		engine::client::ArenaUiPresenter* m_presenter = nullptr;
		bool                              m_enabled   = false;
		uint32_t                          m_viewportW = 0;
		uint32_t                          m_viewportH = 0;
	};
}
