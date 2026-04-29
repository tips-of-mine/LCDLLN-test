#pragma once

#include "engine/net/ChatSystem.h"
#include "engine/platform/Input.h"

#include <cstdint>
#include <string>

namespace engine::client
{
	/// Presents M29.1 chat history + input buffer as a text panel (scroll, filters, slash commands).
	class ChatUiPresenter final
	{
	public:
		ChatUiPresenter() = default;

		~ChatUiPresenter();

		/// Initialize internal buffers; emits logs on success/failure.
		bool Init();

		/// Release presenter state.
		void Shutdown();

		/// Update layout when the viewport changes.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Per-frame input: typing, focus toggle, scroll, channel filter toggles, submit.
		void Update(engine::platform::Input& input, float deltaSeconds);

		/// Append one authoritative line (e.g. decoded \ref engine::server::ChatRelayMessage).
		void PushNetworkLine(const engine::net::ChatMessage& message);

		/// Multi-line debug/panel string for \ref engine::RenderState::chatDebugText.
		std::string BuildPanelText() const;

		/// Phase 3.11 — Texte affichable directement à l'écran post-auth via
		/// \c m_window.SetOverlayText. Plus court et plus lisible que \ref BuildPanelText
		/// (pas de header debug, pas de code couleur ARGB, pas de "filter_mask=0x...").
		/// Format : N lignes "[hh:mm TAG] Sender: text" (filtrées par \ref m_channelFilterMask)
		/// + une ligne d'invite "> _input_" quand le focus est actif, ou "[/] tchatter" sinon.
		/// Retourne une chaîne vide si le presenter n'est pas initialisé.
		std::string BuildHudPanelText() const;

		bool IsChatFocusActive() const { return m_chatFocus; }

		void SetChatFocus(bool focused);

		bool IsInitialized() const { return m_initialized; }

		/// Phase 3.11.1 — Lecteurs publics utilisés par le renderer ImGui chat.
		/// Aucune mutation : le presenter reste seule autorité sur ses buffers.
		const engine::net::ChatHistoryRing& History() const { return m_history; }
		const std::string& InputLine() const { return m_inputLine; }
		uint16_t ChannelFilterMask() const { return m_channelFilterMask; }
		uint32_t ScrollLinesFromEnd() const { return m_scrollLinesFromEnd; }

		/// Phase 3.11.2 — Toggle le filtre d'un canal (bit 0..9). Mirror de la logique
		/// 1-0 clavier dans \ref Update : invariants identiques (XOR sur le bit). Utilisé
		/// par le renderer ImGui pour les chips canal cliquables. No-op si \p channelBit >= 10.
		void ToggleChannelFilter(uint8_t channelBit);

		/// Phase 3.11.3 — Quand un \c ImGui::InputText pilote la saisie, on coupe la branche
		/// keyboard-typing/Enter/Backspace dans \ref Update pour éviter une double insertion.
		/// Escape (unfocus) et scroll restent actifs. \c true par défaut côté ImGui actif,
		/// \c false en fallback texte (Linux ou flag \c render.chat_imgui.enabled=false).
		void SetImGuiInputActive(bool active) { m_imguiInputActive = active; }

		/// Phase 3.11.3 — Remplace directement la ligne de saisie (utilisé par le renderer
		/// pour pousser le buffer \c ImGui::InputText vers le presenter chaque frame).
		/// Tronque silencieusement à \c kMaxInputUtf8Bytes.
		void SetInputLine(std::string_view text);

		/// Phase 3.11.3 — Soumet la ligne de saisie courante (chemin équivalent à Enter
		/// clavier). Utilisé par le renderer quand \c ImGui::InputText retourne \c true
		/// via le flag \c EnterReturnsTrue.
		void SubmitFromUi();

	private:
		void SubmitInputLine();
		void RebuildFilterLegend(std::string& out) const;
		static void PopLastUtf8Codepoint(std::string& utf8);

		engine::net::ChatHistoryRing m_history{};
		bool m_initialized = false;
		bool m_chatFocus = false;
		std::string m_inputLine{};
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		/// Bitmask over \ref engine::net::ChatChannel raw values 0..9 (1 = visible).
		uint16_t m_channelFilterMask = 0x3FFu;
		uint32_t m_scrollLinesFromEnd = 0;
		/// Phase 3.11.3 — \c true quand un \c ImGui::InputText externe pilote la saisie.
		bool m_imguiInputActive = false;
		static constexpr uint32_t kMaxVisibleLines = 18u;
		static constexpr size_t kMaxInputUtf8Bytes = 256u;
	};
}
