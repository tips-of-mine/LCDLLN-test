#include "src/client/chat/ChatUi.h"

#include "src/shared/core/Log.h"

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
			// Slash : convention historique. Enter : convention MMO (WoW, GW2). On
			// active les deux pour ne pas dependre du layout clavier (Slash sur AZERTY
			// FR demande Shift+: ce qui peut surprendre).
			if (input.WasPressed(engine::platform::Key::Slash)
				|| input.WasPressed(engine::platform::Key::Enter))
			{
				m_chatFocus = true;
				LOG_INFO(Core, "[ChatUiPresenter] Chat focus ON (toggle=Slash/Enter)");
			}

			// Bascule des filtres de canaux par CTRL + chiffre (1..0). Le modificateur
			// Ctrl est OBLIGATOIRE : sans lui, les touches 1..0 nues sont réservées à la
			// BARRE D'ACTION (lancement des sorts, Engine slotKey Digit1..Digit0). Sans ce
			// garde, presser 1-5 pour lancer un pouvoir basculait aussi les filtres de
			// canaux (chat non focus) — retour joueur 2026-07-08. Les filtres restent aussi
			// commutables au clic dans l'UI du chat (ToggleChannelFilter).
			if (input.IsDown(engine::platform::Key::Control))
			{
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
						LOG_INFO(Core, "[ChatUiPresenter] Channel filter toggled (Ctrl+{}, channel_index={}, mask=0x{:04X})",
							ch + 1u,
							ch,
							m_channelFilterMask);
					}
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

			// Phase 3.11.3 — Quand un ImGui::InputText pilote la saisie, on saute
			// Enter/Backspace/typing pour éviter une double insertion. ImGui appelle
			// SubmitFromUi via EnterReturnsTrue ; le buffer est sync via SetInputLine.
			if (!m_imguiInputActive)
			{
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

		// Chat MVP — On essaie d'abord d'envoyer au master via la callback installée par
		// l'engine. Si succès : pas d'écho local, le serveur va relayer le message via
		// CHAT_RELAY → PushNetworkLine (sender renseigné côté serveur, sans risque de spoof).
		// Si échec (pas de callback ou pas de session active) : on retombe sur un écho local
		// pour donner un feedback à l'utilisateur même offline.
		// Phase 4 : on transmet targetToken au callback (vide sauf pour /whisper). Le serveur
		// résout le destinataire et stamp le sender côté master ; plus besoin de pre-formater.
		const uint8_t channelWire = engine::net::ToWire(parsed.channel);
		const std::string_view targetToken = parsed.channel == engine::net::ChatChannel::Whisper
			? std::string_view{parsed.whisperTargetToken}
			: std::string_view{};
		bool sent = false;
		if (m_sendCallback)
		{
			sent = m_sendCallback(channelWire, targetToken, parsed.messageBody);
		}

		if (!sent)
		{
			const uint64_t ts = NowUnixMsUtc();
			engine::net::ChatMessage message{};
			message.timestampUnixMs = ts;
			message.channel = parsed.channel;
			message.sender = "Local";
			// En offline, on conserve l'ancien formatage "[to X] body" pour que l'utilisateur
			// voie clairement à qui le whisper était destiné même sans broadcast serveur.
			message.text = parsed.channel == engine::net::ChatChannel::Whisper
				? std::string{"[to "} + parsed.whisperTargetToken + "] " + parsed.messageBody
				: parsed.messageBody;
			m_history.Push(message);
			LOG_INFO(Core, "[ChatUiPresenter] Message echoed locally (offline ; channel_wire={}, ts_ms={})",
				static_cast<unsigned>(channelWire), ts);
		}
		else
		{
			LOG_INFO(Core, "[ChatUiPresenter] Message sent to master (channel_wire={}, has_target={})",
				static_cast<unsigned>(channelWire), !targetToken.empty());
		}

		m_inputLine.clear();
		m_chatFocus = false;
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

	void ChatUiPresenter::ToggleChannelFilter(uint8_t channelBit)
	{
		if (channelBit >= 10u)
			return;
		const uint16_t bit = static_cast<uint16_t>(1u << channelBit);
		m_channelFilterMask = static_cast<uint16_t>(m_channelFilterMask ^ bit);
		LOG_INFO(Core, "[ChatUiPresenter] Channel filter toggled via UI (channel_index={}, mask=0x{:04X})",
			static_cast<unsigned>(channelBit),
			m_channelFilterMask);
	}

	void ChatUiPresenter::SetInputLine(std::string_view text)
	{
		const size_t n = (text.size() < kMaxInputUtf8Bytes) ? text.size() : kMaxInputUtf8Bytes;
		m_inputLine.assign(text.data(), n);
	}

	void ChatUiPresenter::SubmitFromUi()
	{
		if (!m_initialized)
			return;
		SubmitInputLine();
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
				panel += engine::net::FormatTimeHHMMLocal(line.timestampUnixMs);
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

	std::string ChatUiPresenter::BuildHudPanelText() const
	{
		if (!m_initialized)
			return {};

		// Filtre les lignes selon la mask par canal, comme BuildPanelText.
		std::vector<const engine::net::ChatMessage*> filtered;
		filtered.reserve(m_history.Lines().size());
		for (const engine::net::ChatMessage& line : m_history.Lines())
		{
			if (ChannelFilterAllows(m_channelFilterMask, line.channel))
			{
				filtered.push_back(&line);
			}
		}

		std::string out;
		out.reserve(1024);

		if (filtered.empty())
		{
			out += "(pas de messages)\n";
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
				out += engine::net::FormatTimeHHMMLocal(line.timestampUnixMs);
				out += " [";
				out += ChannelTag(line.channel);
				out += "] ";
				out += line.sender;
				out += ": ";
				out += line.text;
				out += '\n';
			}
		}

		// Ligne d'invite : focus actif -> on montre la saisie en cours ; sinon, hint clavier.
		if (m_chatFocus)
		{
			out += "> ";
			out += m_inputLine;
			out += '_';
		}
		else
		{
			out += "[/] pour tchatter  -  [1..0] filtres canal";
		}
		return out;
	}
}
