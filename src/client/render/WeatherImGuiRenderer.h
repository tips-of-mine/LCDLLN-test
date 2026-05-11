#pragma once
// CMANGOS.42 (Phase 4.42 step 3+4) — WeatherImGuiRenderer : 2 modes de
// rendering pour la meteo cote client :
//   1. Window panel "Weather" (toggleable via touche Y / slash /weather) :
//      liste des zones avec kind + intensity bar, boutons Subscribe/Unsubscribe
//      et Set Active (selectionne la zone HUD).
//   2. HUD top-right overlay (toujours rendu si activeZoneId set) : icone
//      + nom de la zone + kind + intensity bar. Pas de titre, non-deplaçable.
//
// Lit l'etat d'un WeatherUiPresenter, dispatch les inputs UI (RequestList /
// Subscribe / Unsubscribe / SetActiveZone) via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class WeatherUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui des composants Weather. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::WeatherUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant et propager les inputs UI
	/// vers le presenter.
	class WeatherImGuiRenderer
	{
	public:
		WeatherImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::WeatherUiPresenter* presenter) { m_presenter = presenter; }

		/// Active/desactive le rendu du panel principal. Le HUD top-right est
		/// independant de ce flag (toujours rendu si activeZoneId set).
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau et du HUD.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Weather" (si IsEnabled()) et le HUD top-right
		/// (si activeZoneId set, independamment de IsEnabled()). A appeler
		/// entre \c ImGui::NewFrame() et \c ImGui::Render(), apres NewFrame,
		/// si le presenter est valide.
		void Render();

		/// Render uniquement le HUD top-right (utilise si on veut le HUD
		/// sans le panel ; appelable independamment de Render() si besoin).
		void RenderHud();

	private:
		/// Dessine le panneau principal (liste zones + boutons + intensity bars).
		void RenderMainPanel();

		engine::client::WeatherUiPresenter* m_presenter = nullptr;
		bool                                m_enabled   = false;
		uint32_t                            m_viewportW = 0;
		uint32_t                            m_viewportH = 0;
	};
}
