#pragma once
// CMANGOS.30 (Phase 5.30 step 3+4) — CinematicImGuiRenderer : overlay
// cinematique cote client (black bars + skip hint).
//
// Pendant le playback, le renderer dessine deux barres noires (top + bottom)
// avec un fade-in 500ms, et un texte centre "Press [Esc] to skip" en bas.
// Si state.isPlaying == false, pas de rendering.
//
// Le renderer ne fait que dessiner : la logique de lecture est dans le
// CinematicUiPresenter. L'input Esc est intercepte cote Engine et transmis
// via presenter->RequestSkip().

#include <cstdint>

namespace engine::client { class CinematicUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui de l'overlay cinematique. Pas de logique de fetch :
	/// celle-ci est dans \ref engine::client::CinematicUiPresenter. Le
	/// renderer ne fait que dessiner l'etat courant.
	class CinematicImGuiRenderer
	{
	public:
		CinematicImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::CinematicUiPresenter* presenter) { m_presenter = presenter; }

		/// Met a jour la viewport pour le placement des barres noires.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render l'overlay (black bars + skip hint) a appeler entre
		/// \c ImGui::NewFrame() et \c ImGui::Render(), si le presenter est
		/// valide et state.isPlaying == true.
		void Render();

	private:
		/// Calcule le facteur de fade-in (0..1) base sur currentTimeMs vs
		/// la duree de fade kFadeInMs.
		///
		/// \param currentTimeMs ts relatif depuis le debut de la sequence.
		/// \return 0.0 au debut, 1.0 apres kFadeInMs.
		float ComputeFadeAlpha(uint64_t currentTimeMs) const;

		engine::client::CinematicUiPresenter* m_presenter = nullptr;
		uint32_t                              m_viewportW = 0;
		uint32_t                              m_viewportH = 0;

		/// Duree du fade-in des black bars en ms. Constante d'esthetique.
		static constexpr uint64_t kFadeInMs = 500u;
	};
}
