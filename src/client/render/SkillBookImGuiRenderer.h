#pragma once
// CMANGOS.39 (Phase 4.39 step 3+4) — SkillBookImGuiRenderer : panneau ImGui
// pour la skill book cote joueur. Lit l'etat d'un SkillBookUiPresenter,
// dispatch les inputs UI vers le presenter via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class SkillBookUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau Skill Book. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::SkillBookUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant et propager les inputs
	/// UI vers le presenter.
	class SkillBookImGuiRenderer
	{
	public:
		SkillBookImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::SkillBookUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Skill Book" + indicateur Use s'il est actif.
		/// A appeler entre \c ImGui::NewFrame() et \c ImGui::Render(), apres
		/// NewFrame, si le presenter est valide et IsEnabled().
		void Render();

	private:
		void RenderListPanel();
		void RenderUseIndicator();

		engine::client::SkillBookUiPresenter* m_presenter = nullptr;
		bool                                  m_enabled   = false;
		uint32_t                              m_viewportW = 0;
		uint32_t                              m_viewportH = 0;
	};
}
