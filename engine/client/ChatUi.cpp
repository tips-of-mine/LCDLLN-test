#include "engine/client/ChatUi.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace engine::client
{
	namespace
	{
		/// Current UTC wall time in milliseconds since Unix epoch.
		uint64_t NowUnixMsUtc()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		}

		/// Short wire tag for one channel (panel text).
		const char* ChannelTag(engine::net::ChatChannel channel)
		{
			switch (channel)
			{
			case engine::net::ChatChannel::Say:
				return "SAY";
			case engine::net::ChatChannel::Yell:
				return "YELL";
			case engine::net::ChatChannel::Whisper:
				return "WSP";
			case engine::net::ChatChannel::Party:
				return "PTY";
			case engine::net::ChatChannel::Guild:
				return "GLD";
			case engine::net::ChatChannel::Zone:
				return "ZON";
			case engine::net::ChatChannel::Global:
				return "GLB";
			case engine::net::ChatChannel::Server:
				return "SRV";
			case engine::net::ChatChannel::Raid:
				return "RAI";
			case engine::net::ChatChannel::Friends:
				return "AMI";
			}

			return "???";
		}

		bool ChannelFilterAllows(uint16_t mask, engine::net::ChatChannel channel)
		{
			const uint32_t bit = 1u << static_cast<uint32_t>(channel);
			return (static_cast<uint32_t>(mask) & bit) != 0u;
		}
	}

	ChatUiPresenter::~ChatUiPresenter()
	{
		Shutdown();
	}

	bool ChatUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ChatUiPresenter] Init ignored: already initialized");
			return true;
		}

		m_inputLine.clear();
		m_chatFocus = false;
		m_channelFilterMask = 0x3FFu;
		m_scrollLinesFromEnd = 0;
		m_initialized = true;
		LOG_INFO(Core, "[ChatUiPresenter] Init OK (history_cap={})", engine::net::ChatHistoryRing::kMaxLines);
		return true;
	}

	void ChatUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_history.Clear();
		m_inputLine.clear();
		m_chatFocus = false;
		LOG_INFO(Core, "[ChatUiPresenter] Destroyed");
	}

	bool ChatUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ChatUiPresenter] SetViewportSize FAILED: presenter not initialized");
			return false;
		}

		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[ChatUiPresenter] SetViewportSize FAILED: invalid viewport {}x{}", width, height);
			return false;
		}

		m_viewportWidth = width;
		m_viewportHeight = height;
		LOG_INFO(Core, "[ChatUiPresenter] Viewport updated ({}x{})", width, height);
		return true;
	}

	void ChatUiPresenter::PopLastUtf8Codepoint(std::string& utf8)
	{
		while (!utf8.empty())
		{
			const unsigned char back = static_cast<unsigned char>(utf8.back());
			utf8.pop_back();
			if ((back & 0xC0u) != 0x80u)
			{
				break;
			}
		}
	}

	void ChatUiPresenter::Update(engine::platform::Input& input, float deltaSeconds)
	{
		(void)deltaSeconds;
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ChatUiPresenter] Update FAILED: presenter not initialized");
			return;
		}

		if (m_viewportWidth == 0 || m_viewportHeight == 0)
		{
			LOG_WARN(Core, "[ChatUiPresenter] Update using fallback: viewport not set");
		}

		if (!m_chatFocus)
		{
			if (input.WasPressed(engine::platform::Key::Slash))
			{
				m_chatFocus = true;
				LOG_INFO(Core, "[ChatUiPresenter] Chat focus ON (toggle=Slash)");
			}

			for (uint32_t ch = 0; ch < 10; ++ch)
			{
				const engine::platform::Key digitKeys[10] = {
					engine::platform::Key::Digit1,
					engine::platform::Key::Digit2,
					engine::platform::Key::Digit3,
					engine::platform::Key::Digit4,
					engine::platform::Key::Digit5,
					engine::platform::Key::Digit6,
					engine::platform::Key::Digit7,
					engine::platform::Key::Digit8,
					engine::platform::Key::Digit9,
					engine::platform::Key::Digit0
				};
				if (input.WasPressed(digitKeys[ch]))
				{
					const uint32_t bit = 1u << ch;
					m_channelFilterMask ^= static_cast<uint16_t>(bit);
					LOG_INFO(Core, "[ChatUiPresenter] Channel filter toggled (channel_index={}, mask=0x{:04X})",
						ch,
						m_channelFilterMask);
				}
			}
		}
		else
		{
			if (input.WasPressed(engine::platform::Key::Escape))
			{
				m_chatFocus = false;
				LOG_INFO(Core, "[ChatUiPresenter] Chat focus OFF (Escape)");
			}

			if (input.WasPressed(engine::platform::Key::Enter))
			{
				SubmitInputLine();
			}

			if (input.WasPressed(engine::platform::Key::Backspace) && !m_inputLine.empty())
			{
				PopLastUtf8Codepoint(m_inputLine);
			}

			std::string pending{};
			input.ConsumePendingTextUtf8(pending);
			if (!pending.empty())
			{
				for (const char ch : pending)
				{
					const auto uch = static_cast<unsigned char>(ch);
					if (uch < 32u && uch != '\t')
					{
						continue;
					}

					if (m_inputLine.size() >= kMaxInputUtf8Bytes)
					{
						LOG_WARN(Core, "[ChatUiPresenter] Input truncated at max_bytes={}", kMaxInputUtf8Bytes);
						break;
					}

					m_inputLine.push_back(ch);
				}
			}

			const int scrollDelta = input.MouseScrollDelta();
			if (scrollDelta != 0)
			{
				if (scrollDelta > 0)
				{
					m_scrollLinesFromEnd += static_cast<uint32_t>(scrollDelta);
				}
				else
				{
					const uint32_t down = static_cast<uint32_t>(-scrollDelta);
					m_scrollLinesFromEnd = (m_scrollLinesFromEnd > down) ? (m_scrollLinesFromEnd - down) : 0u;
				}

				LOG_DEBUG(Core, "[ChatUiPresenter] Scroll updated (lines_from_end={})", m_scrollLinesFromEnd);
			}

			if (input.WasPressed(engine::platform::Key::PageUp))
			{
				m_scrollLinesFromEnd += 4u;
				LOG_DEBUG(Core, "[ChatUiPresenter] PageUp scroll (lines_from_end={})", m_scrollLinesFromEnd);
			}

			if (input.WasPressed(engine::platform::Key::PageDown))
			{
				m_scrollLinesFromEnd = (m_scrollLinesFromEnd > 4u) ? (m_scrollLinesFromEnd - 4u) : 0u;
				LOG_DEBUG(Core, "[ChatUiPresenter] PageDown scroll (lines_from_end={})", m_scrollLinesFromEnd);
			}
		}
	}

	void ChatUiPresenter::PushNetworkLine(const engine::net::ChatMessage& message)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[ChatUiPresenter] PushNetworkLine ignored: not initialized");
			return;
		}

		m_history.Push(message);
		LOG_INFO(Net, "[ChatUiPresenter] Network chat line pushed (channel_wire={}, sender_len={}, text_len={})",
			static_cast<unsigned>(engine::net::ToWire(message.channel)),
			message.sender.size(),
			message.text.size());
	}

	void ChatUiPresenter::SubmitInputLine()
	{
		engine::net::ParsedChatCommand parsed{};
		if (!engine::net::ParseSlashPrefixes(m_inputLine, parsed))
		{
			LOG_WARN(Core, "[ChatUiPresenter] Submit ignored: empty or unparseable line");
			m_inputLine.clear();
			return;
		}

		if (parsed.channel == engine::net::ChatChannel::Whisper && parsed.whisperTargetToken.empty())
		{
			LOG_WARN(Core, "[ChatUiPresenter] Submit ignored: whisper without target token");
			m_inputLine.clear();
			return;
		}

		if (parsed.messageBody.empty())
		{
			LOG_WARN(Core, "[ChatUiPresenter] Submit ignored: empty message body");
			m_inputLine.clear();
			return;
		}

		const uint64_t ts = NowUnixMsUtc();
		engine::net::ChatMessage message{};
		message.timestampUnixMs = ts;
		message.channel = parsed.channel;
		message.sender = "Local";
		message.text = parsed.messageBody;
		if (parsed.channel == engine::net::ChatChannel::Whisper)
		{
			message.text = "[to ";
			message.text += parsed.whisperTargetToken;
			message.text += "] ";
			message.text += parsed.messageBody;
		}

		m_history.Push(message);
		m_inputLine.clear();
		m_chatFocus = false;
		LOG_INFO(Core, "[ChatUiPresenter] Message submitted locally (channel_wire={}, ts_ms={})",
			static_cast<unsigned>(engine::net::ToWire(parsed.channel)),
			ts);
	}

	void ChatUiPresenter::RebuildFilterLegend(std::string& out) const
	{
		out += "filter_mask=0x";
		char hex[5]{};
		std::snprintf(hex, sizeof(hex), "%04X", static_cast<unsigned>(m_channelFilterMask));
		out += hex;
		out += " [1-9,0 toggles when unfocused] ";
	}

	void ChatUiPresenter::SetChatFocus(bool focused)
	{
		m_chatFocus = focused;
		LOG_INFO(Core, "[ChatUiPresenter] Chat focus set ({})", focused ? "true" : "false");
	}

	std::string ChatUiPresenter::BuildPanelText() const
	{
		std::string panel;
		panel.reserve(1024);
		panel += "[ChatUi]";
		panel += " focus=";
		panel += m_chatFocus ? "true" : "false";
		panel += " ";
		RebuildFilterLegend(panel);
		panel += "scroll_end=";
		panel += std::to_string(m_scrollLinesFromEnd);
		panel += "\n";
		panel += "colors=#AARRGGBB prefix (SAY=white,YELL=red,WSP=pink,PTY=blue,GLD=green,ZON=cyan,GLB=gold,SRV=orange,RAI=red-orange,AMI=light-green)\n";

		std::vector<const engine::net::ChatMessage*> filtered;
		filtered.reserve(m_history.Lines().size());
		for (const engine::net::ChatMessage& line : m_history.Lines())
		{
			if (ChannelFilterAllows(m_channelFilterMask, line.channel))
			{
				filtered.push_back(&line);
			}
		}

		if (filtered.empty())
		{
			panel += "(no messages)\n";
		}
		else
		{
			const uint32_t total = static_cast<uint32_t>(filtered.size());
			const uint32_t maxLines = kMaxVisibleLines;
			uint32_t begin = 0;
			if (total > maxLines)
			{
				const uint32_t maxStart = total - maxLines;
				const uint32_t scrollClamp = std::min(m_scrollLinesFromEnd, maxStart);
				begin = maxStart - scrollClamp;
			}

			const uint32_t end = std::min<uint32_t>(total, begin + maxLines);
			for (uint32_t i = begin; i < end; ++i)
			{
				const engine::net::ChatMessage& line = *filtered[i];
				char colorHex[12]{};
				const uint32_t argb = engine::net::ChannelColorArgb(line.channel);
				std::snprintf(colorHex,
					sizeof(colorHex),
					"#%08X",
					static_cast<unsigned int>(argb));
				panel += colorHex;
				panel += "|";
				panel += engine::net::FormatTimeHHMMUtc(line.timestampUnixMs);
				panel += " [";
				panel += ChannelTag(line.channel);
				panel += "] ";
				panel += line.sender;
				panel += ": ";
				panel += line.text;
				panel += "\n";
			}
		}

		panel += m_chatFocus ? "> " : ": ";
		panel += m_inputLine;
		panel += "_\n";
		return panel;
	}
}
