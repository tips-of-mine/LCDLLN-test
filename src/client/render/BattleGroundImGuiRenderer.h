#pragma once
// CMANGOS.10 (Phase 5 step 3+4) — BattleGroundImGuiRenderer : panneau ImGui
// pour la fenetre BattleGround cote joueur. Lit l'etat d'un
// BattleGroundUiPresenter, dispatch les inputs UI (RequestList / Queue
// Alliance / Queue Horde / LeaveQueue / LeaveMatch) via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class BattleGroundUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau BattleGround. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::BattleGroundUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant et propager les inputs UI
	/// vers le presenter.
	class BattleGroundImGuiRenderer
	{
	public:
		BattleGroundImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::BattleGroundUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "BattleGround" + scoreboard si match actif. A
		/// appeler entre \c ImGui::NewFrame() et \c ImGui::Render(), apres
		/// NewFrame, si le presenter est valide et IsEnabled().
		void Render();

	private:
		/// Dessine le panneau principal (liste BG + queue/leave + last result).
		void RenderMainPanel();

		/// Dessine le scoreboard quand un match est actif.
		void RenderActiveMatchPanel();

		engine::client::BattleGroundUiPresenter* m_presenter = nullptr;
		bool                                     m_enabled   = false;
		uint32_t                                 m_viewportW = 0;
		uint32_t                                 m_viewportH = 0;
	};
}
