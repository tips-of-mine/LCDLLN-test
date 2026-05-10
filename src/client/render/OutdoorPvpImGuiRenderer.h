#pragma once
// CMANGOS.36 (Phase 5.36 step 3+4) — OutdoorPvpImGuiRenderer : panneau ImGui
// pour la fenetre OutdoorPvp cote joueur. Lit l'etat d'un
// OutdoorPvpUiPresenter, dispatch les inputs UI (RequestList / Subscribe /
// Unsubscribe / StartCapture par objectif) via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class OutdoorPvpUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau OutdoorPvp. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::OutdoorPvpUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant et propager les inputs UI
	/// vers le presenter.
	class OutdoorPvpImGuiRenderer
	{
	public:
		OutdoorPvpImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::OutdoorPvpUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Outdoor PvP". A appeler entre \c ImGui::NewFrame()
		/// et \c ImGui::Render(), apres NewFrame, si le presenter est valide
		/// et IsEnabled().
		void Render();

	private:
		/// Dessine le panneau principal (liste zones avec collapsing headers
		/// + objectives + boutons + toast last result).
		void RenderMainPanel();

		engine::client::OutdoorPvpUiPresenter* m_presenter = nullptr;
		bool                                   m_enabled   = false;
		uint32_t                               m_viewportW = 0;
		uint32_t                               m_viewportH = 0;
	};
}
