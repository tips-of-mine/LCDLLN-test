#pragma once
// CMANGOS.18 (Phase 3.18 step 4) — MailImGuiRenderer : panel ImGui
// pour la boite mail. Lit l'etat d'un MailUiPresenter, dispatch les
// inputs UI vers le presenter via accesseurs.

#include <cstdint>

namespace engine::client { class MailUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui de la boite mail. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::MailUiPresenter. Le renderer ne
	/// fait que dessiner l'etat courant et propager les inputs UI vers le
	/// presenter via setters / methodes.
	class MailImGuiRenderer
	{
	public:
		MailImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::MailUiPresenter* presenter) { m_presenter = presenter; }

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel mailbox + compose dialog (si \c isComposeOpen). A appeler
		/// entre \c ImGui::NewFrame() et \c ImGui::Render(), apres NewFrame, si
		/// le presenter est valide et IsEnabled().
		void Render();

	private:
		void RenderInboxPanel();
		void RenderComposeDialog();

		engine::client::MailUiPresenter* m_presenter = nullptr;
		bool                              m_enabled  = false;
		uint32_t                          m_viewportW = 0;
		uint32_t                          m_viewportH = 0;

		/// Buffers C-string consommes par ImGui::InputText. Synchronises depuis
		/// le presenter chaque frame (sens UI -> presenter via setters).
		char m_recipientBuf[64]{};
		char m_subjectBuf[256]{};
		char m_bodyBuf[8192]{};
	};
}
