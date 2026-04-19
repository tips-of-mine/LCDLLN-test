#pragma once

#include "engine/net/ChatSystem.h"
#include "engine/platform/Input.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Four UI tabs shown in the bottom-left chat panel (design system — HudOverlay).
	enum class ChatUiTab : uint8_t
	{
		General = 0, ///< SAY / YELL / PARTY / ZONE / GLOBAL / SERVER / RAID / FRIENDS
		Trade   = 1, ///< ZONE + GLOBAL (commerce)
		Guild   = 2, ///< GUILD only
		Whisper = 3, ///< WHISPER / MP only
	};

	/// One rendered chat line: bracketed sender name with ARGB color + italic body.
	struct ChatMessageLine
	{
		std::string senderLabel;                   ///< e.g. "[Aldric]"; empty for server lines.
		std::string messageText;
		uint32_t    senderColorArgb = 0xFFFFFFFFu; ///< ARGB32 applied to senderLabel.
	};

	/// Fully resolved chat panel state ready for a renderer to consume.
	struct ChatPanelState
	{
		ChatUiTab                    activeTab = ChatUiTab::General;
		std::vector<ChatMessageLine> visibleLines; ///< Scrolled + filtered window.
		std::string                  inputDraft;
		bool                         focused    = false;
		// Pixel-space bounds (bottom-left anchor, 360 px wide per design spec).
		float panelX      = 18.f;
		float panelY      = 0.f;
		float panelWidth  = 360.f;
		float panelHeight = 200.f;
		bool  layoutValid = false;
	};

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

		bool IsChatFocusActive() const { return m_chatFocus; }

		void SetChatFocus(bool focused);

		bool IsInitialized() const { return m_initialized; }

		// ---- Design system: tab selection + panel state ----

		/// Switch the active chat tab (updates visible line set immediately).
		void SetActiveTab(ChatUiTab tab);
		ChatUiTab             GetActiveTab()   const { return m_activeTab;   }
		const ChatPanelState& GetPanelState()  const { return m_panelState;  }

	private:
		void SubmitInputLine();
		void RebuildFilterLegend(std::string& out) const;
		static void PopLastUtf8Codepoint(std::string& utf8);

		/// Recompute m_panelState from current history, tab, scroll and input buffer.
		void RebuildPanelState();

		/// Channel bitmask for a given UI tab.
		static uint16_t TabChannelMask(ChatUiTab tab);

		/// ARGB32 color for the sender label of one message (design system palette).
		static uint32_t SenderColorArgb(const engine::net::ChatMessage& msg);

		engine::net::ChatHistoryRing m_history{};
		bool m_initialized = false;
		bool m_chatFocus = false;
		std::string m_inputLine{};
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		/// Bitmask over \ref engine::net::ChatChannel raw values 0..9 (1 = visible).
		uint16_t m_channelFilterMask = 0x3FFu;
		uint32_t m_scrollLinesFromEnd = 0;
		static constexpr uint32_t kMaxVisibleLines = 18u;
		static constexpr size_t kMaxInputUtf8Bytes = 256u;

		ChatUiTab      m_activeTab  = ChatUiTab::General;
		ChatPanelState m_panelState{};
	};
}
