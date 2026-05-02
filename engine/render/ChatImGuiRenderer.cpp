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

	void ChatImGuiRenderer::Render(float viewportW, float viewportH, bool inWorldShard)
	{
		if (m_chat == nullptr || !m_chat->IsInitialized())
			return;

		// Geometrie configurable. Defaults augmentes a 640x300 px ancre en bas-gauche, marge 24 px,
		// pour que le panneau soit clairement visible (plaintes utilisateurs : "le chat ne s'affiche pas").
		const float panelW = m_cfg ? static_cast<float>(m_cfg->GetInt("render.chat_imgui.width_px", 640)) : 640.f;
		const float panelH = m_cfg ? static_cast<float>(m_cfg->GetInt("render.chat_imgui.height_px", 300)) : 300.f;
		const float margin = m_cfg ? static_cast<float>(m_cfg->GetInt("render.chat_imgui.anchor_margin_px", 24)) : 24.f;

		const float posX = margin;
		const float posY = std::max(0.f, viewportH - panelH - margin);

		ImGui::SetNextWindowPos(ImVec2(posX, posY));
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
		// Background opaque (alpha=0.95) au lieu de 0.78 pour bien le voir sur fonds clairs et sombres.
		ImGui::SetNextWindowBgAlpha(0.95f);
		// Force le panneau chat au PREMIER PLAN : utile sur les ecrans pre-EnterWorld
		// ou le panneau auth occupe TOUT le viewport en alpha=1.0 et masque le chat
		// si l'ordre d'empilement ImGui ne le met pas devant. Sans focus capture
		// (NoBringToFrontOnFocus + NoFocusOnAppearing en flags) pour ne pas voler
		// l'input clavier de l'auth.
		ImGui::SetNextWindowFocus();

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.95f)));
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
			//
			// Restriction utilisateur : in-world on n'expose que Global / Zone / Friends
			// (les seuls canaux fonctionnels demandes). Les autres (Say, Yell, Whisper,
			// Party, Guild, Server, Raid) restent supportes par le wire mais ne sont
			// pas exposes en UI tant que les fonctionnalites associees (groupe, guilde,
			// raid...) ne sont pas branchees cote serveur.
			// Set de canaux exposes selon le contexte. Demande utilisateur :
			//   * Post-auth mais pas encore in-world : Global + Friends.
			//   * Une fois le royaume rejoint (in-world)  : + Zone.
			static constexpr uint8_t kChannelsPostAuth[] = {
				static_cast<uint8_t>(engine::net::ChatChannel::Global),  // 6
				static_cast<uint8_t>(engine::net::ChatChannel::Friends), // 9
			};
			static constexpr uint8_t kChannelsPostShard[] = {
				static_cast<uint8_t>(engine::net::ChatChannel::Global),  // 6
				static_cast<uint8_t>(engine::net::ChatChannel::Zone),    // 5
				static_cast<uint8_t>(engine::net::ChatChannel::Friends), // 9
			};
			const uint8_t* kVisibleChannels = inWorldShard ? kChannelsPostShard : kChannelsPostAuth;
			const size_t   kVisibleChannelsCount = inWorldShard
				? (sizeof(kChannelsPostShard) / sizeof(kChannelsPostShard[0]))
				: (sizeof(kChannelsPostAuth) / sizeof(kChannelsPostAuth[0]));
			const uint16_t mask = m_chat->ChannelFilterMask();
			ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.20f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IV(LnTheme::AccentDim(0.35f)));
			for (size_t k = 0; k < kVisibleChannelsCount; ++k)
			{
				const uint8_t i = kVisibleChannels[k];
				if (k > 0) ImGui::SameLine(0.f, 4.f);
				const bool visible = (mask & (1u << i)) != 0u;
				const ImVec4 color = visible
					? ArgbToIm(engine::net::ChannelColorArgb(static_cast<engine::net::ChatChannel>(i)))
					: IV(LnTheme::kMuted);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				char buttonId[32];
				// Pas de crochets [ ] autour du tag : Windlass.ttf n'a pas ces glyphes et
				// ImGui les rendait comme "?" (cf. fix global glyph range PR #419).
				std::snprintf(buttonId, sizeof(buttonId), "%s##ch%u",
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
				ImGui::TextColored(color, "%s %s %s: %s",
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
				ImGui::TextUnformatted("/ pour tchatter  -  1..0 filtres canal");
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
	void ChatImGuiRenderer::Render(float, float, bool) {}
}

#endif
