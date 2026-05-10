#pragma once
// CMANGOS.33 (Phase 5.33 step 3+4) — LfgImGuiRenderer : panneau ImGui pour
// la fenetre LookForGroup cote joueur. Lit l'etat d'un LfgUiPresenter,
// dispatch les inputs UI (Queue / Leave / Status / Accept / Reject) via
// accesseurs / methodes.

#include <cstdint>

namespace engine::client { class LfgUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau LFG. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::LfgUiPresenter. Le renderer ne
	/// fait que dessiner l'etat courant et propager les inputs UI vers le
	/// presenter.
	class LfgImGuiRenderer
	{
	public:
		LfgImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::LfgUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "LFG" + modal proposal s'il est actif. A appeler
		/// entre \c ImGui::NewFrame() et \c ImGui::Render(), apres NewFrame,
		/// si le presenter est valide et IsEnabled().
		void Render();

	private:
		/// Dessine le panneau principal (Queue / Leave + status courant).
		void RenderMainPanel();

		/// Dessine le modal popup quand un proposal est en attente.
		void RenderProposalModal();

		engine::client::LfgUiPresenter* m_presenter = nullptr;
		bool                            m_enabled   = false;
		uint32_t                        m_viewportW = 0;
		uint32_t                        m_viewportH = 0;

		/// Etat de l'UI : choix utilisateur courant (avant click "Queue").
		/// Persistant entre frames pour que les radio/combo gardent leur
		/// valeur tant que le user n'a pas valide.
		int      m_uiSelectedRoleIdx    = 0; ///< 0=Tank, 1=Healer, 2=Damage.
		uint32_t m_uiSelectedDungeonId  = 1; ///< id selectionne dans le combo.
	};
}
