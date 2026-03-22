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

		bool IsChatFocusActive() const { return m_chatFocus; }

		void SetChatFocus(bool focused);

		bool IsInitialized() const { return m_initialized; }

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
		/// Bitmask over \ref engine::net::ChatChannel raw values 0..7 (1 = visible).
		uint8_t m_channelFilterMask = 0xFFu;
		uint32_t m_scrollLinesFromEnd = 0;
		static constexpr uint32_t kMaxVisibleLines = 18u;
		static constexpr size_t kMaxInputUtf8Bytes = 256u;
	};
}
