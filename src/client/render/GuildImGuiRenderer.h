#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — GuildImGuiRenderer : panneau ImGui
// pour la fenetre Guildes. Top section : table des guildes (Name / Leader /
// Members) avec un bouton Select par ligne. Bottom section (visible si
// selectedGuildId set) : 4 tabs (Members, Permissions, Bank, MOTD).
//
// Le toast 5s sur dernier MotdUpdate reçu est rendu independamment du flag
// IsEnabled() (les push peuvent arriver panneau ferme).
//
// Lit l'etat d'un GuildUiPresenter, dispatch les inputs UI (RequestList /
// SelectGuild) via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class GuildUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau Guildes. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::GuildUiPresenter. Le renderer
	/// ne fait que dessiner l'etat courant et propager les inputs UI vers
	/// le presenter.
	class GuildImGuiRenderer
	{
	public:
		GuildImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::GuildUiPresenter* presenter) { m_presenter = presenter; }

		/// Active/desactive le rendu du panel principal.
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Guilds" (si IsEnabled()) et le toast 5s pour le
		/// dernier MotdUpdate reçu (rendu independamment du flag IsEnabled()
		/// puisque les push peuvent arriver panneau ferme). A appeler entre
		/// \c ImGui::NewFrame() et \c ImGui::Render(), apres NewFrame, si le
		/// presenter est valide.
		void Render();

	private:
		/// Dessine le panneau principal (top : liste guildes ; bottom :
		/// 4 tabs detail).
		void RenderMainPanel();

		/// Dessine le toast 5s en bas-droite si lastMotd* est recent.
		void RenderToast();

		engine::client::GuildUiPresenter* m_presenter = nullptr;
		bool                              m_enabled   = false;
		uint32_t                          m_viewportW = 0;
		uint32_t                          m_viewportH = 0;
	};
}
