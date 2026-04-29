#include "engine/render/ChatImGuiRenderer.h"

#include "engine/client/ChatUi.h"
#include "engine/core/Config.h"
#include "engine/net/ChatSystem.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

		/// Convertit ARGB8888 (cf. \ref engine::net::ChannelColorArgb) en ImVec4.
		/// ImGui n'a pas d'overload qui prend un ARGB direct ; on décompose via masques.
		ImVec4 ArgbToIm(uint32_t argb)
		{
			const float a = static_cast<float>((argb >> 24) & 0xFFu) / 255.f;
			const float r = static_cast<float>((argb >> 16) & 0xFFu) / 255.f;
			const float g = static_cast<float>((argb >>  8) & 0xFFu) / 255.f;
			const float b = static_cast<float>((argb >>  0) & 0xFFu) / 255.f;
			return ImVec4(r, g, b, a);
		}

		const char* ChannelTag(engine::net::ChatChannel ch)
		{
			using engine::net::ChatChannel;
			switch (ch)
			{
				case ChatChannel::Say:     return "SAY";
				case ChatChannel::Yell:    return "YEL";
				case ChatChannel::Whisper: return "WSP";
				case ChatChannel::Party:   return "PTY";
				case ChatChannel::Guild:   return "GLD";
				case ChatChannel::Zone:    return "ZON";
				case ChatChannel::Global:  return "GLB";
				case ChatChannel::Server:  return "SRV";
				case ChatChannel::Raid:    return "RAI";
				case ChatChannel::Friends: return "AMI";
			}
			return "???";
		}
	}

	void ChatImGuiRenderer::BindChatUi(engine::client::ChatUiPresenter* presenter, const engine::core::Config* cfg)
	{
		m_chat = presenter;
		m_cfg = cfg;
	}

	void ChatImGuiRenderer::Render(float viewportW, float viewportH)
	{
		if (m_chat == nullptr || !m_chat->IsInitialized())
			return;

		// Géométrie configurable. Defaults : 520×220 px ancré en bas-gauche, marge 16 px.
		const float panelW = m_cfg ? static_cast<float>(m_cfg->GetInt("render.chat_imgui.width_px", 520)) : 520.f;
		const float panelH = m_cfg ? static_cast<float>(m_cfg->GetInt("render.chat_imgui.height_px", 220)) : 220.f;
		const float margin = m_cfg ? static_cast<float>(m_cfg->GetInt("render.chat_imgui.anchor_margin_px", 16)) : 16.f;

		const float posX = margin;
		const float posY = std::max(0.f, viewportH - panelH - margin);

		ImGui::SetNextWindowPos(ImVec2(posX, posY));
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
		ImGui::SetNextWindowBgAlpha(0.78f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.78f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoNav;

		if (ImGui::Begin("##ln_chat_panel", nullptr, flags))
		{
			// Phase 3.11.2 — Chips canal cliquables. Click toggle le bit via
			// ChatUiPresenter::ToggleChannelFilter (mirror du raccourci 1-0 clavier dans
			// ChatUiPresenter::Update). Couleur : ARGB du canal si visible, gris atténué si masqué.
			const uint16_t mask = m_chat->ChannelFilterMask();
			ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.20f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IV(LnTheme::AccentDim(0.35f)));
			for (uint8_t i = 0; i < 10u; ++i)
			{
				if (i > 0) ImGui::SameLine(0.f, 4.f);
				const bool visible = (mask & (1u << i)) != 0u;
				const ImVec4 color = visible
					? ArgbToIm(engine::net::ChannelColorArgb(static_cast<engine::net::ChatChannel>(i)))
					: IV(LnTheme::kMuted);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				char buttonId[32];
				std::snprintf(buttonId, sizeof(buttonId), "[%s]##ch%u",
					ChannelTag(static_cast<engine::net::ChatChannel>(i)), static_cast<unsigned>(i));
				if (ImGui::SmallButton(buttonId))
				{
					m_chat->ToggleChannelFilter(i);
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s : clic pour %s (raccourci %u)",
						ChannelTag(static_cast<engine::net::ChatChannel>(i)),
						visible ? "masquer" : "afficher",
						static_cast<unsigned>((i + 1u) % 10u));
				}
			}
			ImGui::PopStyleColor(3);
			ImGui::Separator();

			// Zone scrollable : on calcule la hauteur en réservant 28 px pour la ligne d'invite/saisie en bas.
			const float bottomBar = 28.f;
			const float scrollH = std::max(40.f, ImGui::GetContentRegionAvail().y - bottomBar);

			ImGui::BeginChild("##ln_chat_scroll", ImVec2(0.f, scrollH), false,
				ImGuiWindowFlags_HorizontalScrollbar);

			const auto& lines = m_chat->History().Lines();

			// Filtre côté renderer : on saute les lignes hors mask sans toucher au presenter.
			for (const auto& msg : lines)
			{
				const auto chBit = static_cast<uint8_t>(msg.channel);
				if (chBit < 10u && (mask & (1u << chBit)) == 0u)
					continue;

				const ImVec4 color = ArgbToIm(engine::net::ChannelColorArgb(msg.channel));
				const std::string hhmm = engine::net::FormatTimeHHMMUtc(msg.timestampUnixMs);
				const char* tag = ChannelTag(msg.channel);
				ImGui::TextColored(color, "%s [%s] %s: %s",
					hhmm.c_str(), tag, msg.sender.c_str(), msg.text.c_str());
			}

			// Auto-scroll bottom uniquement si l'utilisateur n'a pas remonté manuellement
			// (ChatUiPresenter::m_scrollLinesFromEnd > 0 = remonté ; 0 = en bas).
			if (m_chat->ScrollLinesFromEnd() == 0u)
			{
				ImGui::SetScrollHereY(1.f);
			}
			ImGui::EndChild();

			ImGui::Separator();

			// Phase 3.11.3 — Vrai ImGui::InputText quand le focus est actif.
			// Le presenter passe en mode "ImGui owns input" (cf. SetImGuiInputActive depuis
			// Engine.cpp) pour ne pas dupliquer la saisie. Sync entre m_inputBuf et le
			// presenter chaque frame : 1) si l'InputLine() externe a changé sans nous, on
			// recopie ; 2) après l'InputText, on pousse m_inputBuf -> SetInputLine.
			const bool focused = m_chat->IsChatFocusActive();
			const std::string& presenterLine = m_chat->InputLine();
			if (presenterLine.size() > sizeof(m_inputBuf) - 1u
				|| presenterLine != std::string_view(m_inputBuf))
			{
				const size_t n = (presenterLine.size() < sizeof(m_inputBuf) - 1u)
					? presenterLine.size() : (sizeof(m_inputBuf) - 1u);
				if (n > 0u)
					std::memcpy(m_inputBuf, presenterLine.data(), n);
				m_inputBuf[n] = '\0';
			}

			if (focused)
			{
				// Première frame de focus : pousser le focus clavier sur l'InputText.
				if (!m_lastFocus)
				{
					ImGui::SetKeyboardFocusHere();
				}

				ImGui::PushStyleColor(ImGuiCol_Text,    IV(LnTheme::kAccent));
				ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border,  IV(LnTheme::kBorder));
				ImGui::Text("> ");
				ImGui::SameLine(0.f, 4.f);
				ImGui::SetNextItemWidth(-FLT_MIN); // remplit la ligne restante
				const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue
					| ImGuiInputTextFlags_AutoSelectAll;
				if (ImGui::InputText("##ln_chat_input", m_inputBuf, sizeof(m_inputBuf), flags))
				{
					m_chat->SetInputLine(m_inputBuf);
					m_chat->SubmitFromUi();
					m_inputBuf[0] = '\0';
				}
				else
				{
					// Push permanent du buffer vers le presenter (l'utilisateur tape sans Enter).
					m_chat->SetInputLine(m_inputBuf);
				}
				ImGui::PopStyleColor(3);
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::TextUnformatted("[/] pour tchatter  -  [1..0] filtres canal");
				ImGui::PopStyleColor();
			}

			m_lastFocus = focused;
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void ChatImGuiRenderer::BindChatUi(engine::client::ChatUiPresenter*, const engine::core::Config*) {}
	void ChatImGuiRenderer::Render(float, float) {}
}

#endif
