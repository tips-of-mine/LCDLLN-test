#pragma once

#include <cstdint>

namespace engine::client { class ChatUiPresenter; }
namespace engine::core   { class Config; }

namespace engine::render
{
	/// Phase 3.11.1 — Rendu ImGui du panneau chat post-auth (Windows uniquement,
	/// car le contexte ImGui est porté par WorldEditorImGui qui n'existe que sous WIN32).
	/// Affichage seul : la logique d'input (focus '/', filtres 1-0, scroll, submit Enter)
	/// reste dans \ref engine::client::ChatUiPresenter::Update. Le renderer lit l'historique,
	/// la ligne en cours et le mask de filtres via les accesseurs publics const ajoutés en 3.11.1.
	///
	/// Position : panneau ancré en bas-gauche, taille configurable via
	/// \c render.chat_imgui.{width_px, height_px, anchor_margin_px}.
	class ChatImGuiRenderer final
	{
	public:
		/// Bind les pointeurs (non possédés). À appeler une fois après init ImGui.
		void BindChatUi(engine::client::ChatUiPresenter* presenter, const engine::core::Config* cfg);

		/// Émet les commandes ImGui pour la frame courante. Suppose que
		/// \c m_worldEditorImGui->NewFrame a déjà été appelé.
		/// No-op si le presenter n'est pas bindé ou pas initialisé.
		void Render(float viewportW, float viewportH);

	private:
		engine::client::ChatUiPresenter* m_chat = nullptr;
		const engine::core::Config*      m_cfg  = nullptr;

		/// Mémorise le focus de la frame précédente pour détecter la transition
		/// non-focus → focus (et faire ImGui::SetKeyboardFocusHere sur l'input).
		bool m_lastFocus = false;

		/// Phase 3.11.3 — Buffer mutable consommé par \c ImGui::InputText.
		/// Synchronisé avec \c ChatUiPresenter::InputLine chaque frame.
		/// 257 octets = \c ChatUiPresenter::kMaxInputUtf8Bytes (256) + terminateur NUL.
		char m_inputBuf[257]{};
	};
}
