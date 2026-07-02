#include "src/client/render/GmTicketImGuiRenderer.h"

#include "src/client/gmtickets/GmTicketUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;

		/// Rend une chaine "il y a X minutes/heures/jours" a partir d'un timestamp UTC ms.
		/// Pour V1 on reste simple : pas d'i18n, hardcode FR. Si timestamp vide ou futur,
		/// retourne "—".
		std::string FormatRelativeTime(uint64_t ts)
		{
			if (ts == 0)
				return "-";
			using namespace std::chrono;
			const uint64_t nowMs = static_cast<uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
			if (ts > nowMs)
				return "a l'instant";
			const uint64_t deltaMs = nowMs - ts;
			const uint64_t deltaSec = deltaMs / 1000ull;
			char buf[64]{};
			if (deltaSec < 60ull)
				std::snprintf(buf, sizeof(buf), "il y a %us", static_cast<unsigned>(deltaSec));
			else if (deltaSec < 3600ull)
				std::snprintf(buf, sizeof(buf), "il y a %u min", static_cast<unsigned>(deltaSec / 60ull));
			else if (deltaSec < 86400ull)
				std::snprintf(buf, sizeof(buf), "il y a %uh", static_cast<unsigned>(deltaSec / 3600ull));
			else
				std::snprintf(buf, sizeof(buf), "il y a %uj", static_cast<unsigned>(deltaSec / 86400ull));
			return buf;
		}

		/// Convertit l'etat numerique (cf. TicketState) en libelle FR. Le client ne
		/// connait pas l'enum cote serveur, on duplique ici les 4 valeurs canoniques.
		const char* StateLabel(uint8_t state)
		{
			switch (state)
			{
			case 0u: return "Ouvert";
			case 1u: return "Assigne GM";
			case 2u: return "Resolu";
			case 3u: return "Annule";
			default: return "?";
			}
		}

		/// Charge proprement un std::string dans un buffer C-string fixe (avec NUL).
		void LoadStringIntoBuffer(const std::string& src, char* dst, size_t dstSize)
		{
			if (dstSize == 0)
				return;
			const size_t n = (src.size() < dstSize - 1) ? src.size() : (dstSize - 1);
			if (n > 0)
				std::memcpy(dst, src.data(), n);
			dst[n] = '\0';
		}
	}

	void GmTicketImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		RenderListPanel();
		RenderComposeDialog();
	}

	void GmTicketImGuiRenderer::RenderListPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre bottom-right, 500x300.
		const float panelW = 500.f;
		const float panelH = 300.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - panelW - margin);
		const float posY = std::max(0.f, vpH - panelH - margin);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Support GM##ln_gmtickets_panel", nullptr, flags))
		{
			// Banner notification : un GM vient de resoudre un ticket.
			if (state.lastResolvedNotificationTicketId.has_value())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.f));
				ImGui::TextWrapped("Un GM a resolu votre ticket #%" PRIu64 ".",
					*state.lastResolvedNotificationTicketId);
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			// Erreur transitoire (rouge) — non bloquante.
			if (!state.lastErrorText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastErrorText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			// Toolbar : Nouveau ticket + Refresh.
			if (ImGui::Button("Nouveau ticket"))
			{
				m_presenter->OpenCompose();
			}
			ImGui::SameLine();
			if (ImGui::Button("Rafraichir"))
			{
				m_presenter->RequestMyTickets();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%zu ticket%s)",
				state.mine.size(), state.mine.size() > 1 ? "s" : "");

			ImGui::Separator();

			if (state.isListLoading)
			{
				ImGui::TextUnformatted("Chargement...");
			}
			else if (state.mine.empty())
			{
				ImGui::TextDisabled("Aucun ticket actif.");
			}
			else
			{
				const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
					| ImGuiTableFlags_RowBg
					| ImGuiTableFlags_ScrollY
					| ImGuiTableFlags_Resizable;
				const float tableH = 200.f;
				if (ImGui::BeginTable("##ln_gmtickets_list", 5, tableFlags, ImVec2(0.f, tableH)))
				{
					ImGui::TableSetupColumn("ID",     ImGuiTableColumnFlags_WidthFixed,   60.f);
					ImGui::TableSetupColumn("Cree",   ImGuiTableColumnFlags_WidthFixed,   90.f);
					ImGui::TableSetupColumn("Etat",   ImGuiTableColumnFlags_WidthFixed,   90.f);
					ImGui::TableSetupColumn("Resolu", ImGuiTableColumnFlags_WidthFixed,   90.f);
					ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableHeadersRow();

					for (const auto& t : state.mine)
					{
						ImGui::TableNextRow();
						ImGui::PushID(static_cast<int>(t.id));

						ImGui::TableSetColumnIndex(0);
						ImGui::Text("#%" PRIu64, t.id);

						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(FormatRelativeTime(t.createdTsMs).c_str());

						ImGui::TableSetColumnIndex(2);
						ImGui::TextUnformatted(StateLabel(t.state));

						ImGui::TableSetColumnIndex(3);
						if (t.state == 2u && t.resolvedTsMs > 0u)
							ImGui::TextUnformatted(FormatRelativeTime(t.resolvedTsMs).c_str());
						else
							ImGui::TextDisabled("-");

						ImGui::TableSetColumnIndex(4);
						// Le bouton "Annuler" est dispo seulement quand l'etat
						// est Ouvert (state == 0). Sur les autres etats, on
						// laisse vide pour eviter l'attente d'un AlreadyResolved
						// / NotOwner cote serveur.
						if (t.state == 0u)
						{
							if (ImGui::SmallButton("Annuler"))
							{
								m_presenter->CancelTicket(t.id);
							}
						}
						else
						{
							ImGui::TextDisabled("-");
						}

						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void GmTicketImGuiRenderer::RenderComposeDialog()
	{
		const auto& state = m_presenter->GetState();

		// Synchro buffer ImGui <- presenter quand la dialog s'ouvre. Une fois
		// ouverte on laisse ImGui controller le buffer (push -> presenter via
		// SetComposeBody chaque frame) sans re-charger l'etat depuis le
		// presenter sinon on ecraserait la saisie.
		static bool s_wasOpen = false;
		if (state.isComposeOpen && !s_wasOpen)
		{
			LoadStringIntoBuffer(state.composeBody, m_bodyBuf, sizeof(m_bodyBuf));
			ImGui::OpenPopup("Nouveau ticket support##ln_gmtickets_compose");
		}
		s_wasOpen = state.isComposeOpen;

		// Centre la fenetre modale.
		ImVec2 center;
		center.x = (m_viewportW > 0) ? static_cast<float>(m_viewportW) * 0.5f : 640.f;
		center.y = (m_viewportH > 0) ? static_cast<float>(m_viewportH) * 0.5f : 360.f;
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(520.f, 360.f), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal("Nouveau ticket support##ln_gmtickets_compose", nullptr,
			ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextWrapped("Decrivez votre probleme avec autant de details que possible "
				"(personnage, zone, ce qui s'est passe). Un GM le traitera des que possible.");
			ImGui::Spacing();

			if (ImGui::InputTextMultiline("##ln_gmtickets_body", m_bodyBuf, sizeof(m_bodyBuf),
				ImVec2(-FLT_MIN, 200.f)))
			{
				m_presenter->SetComposeBody(m_bodyBuf);
			}

			// Compteur d'octets (pas de chars unicode-aware, cote serveur on
			// limite par octets aussi : <= kMaxGmTicketBodyBytes = 4096).
			const size_t curBytes = std::strlen(m_bodyBuf);
			ImGui::TextDisabled("%zu / 4096 octets", curBytes);

			ImGui::Separator();
			const bool canSend = (curBytes > 0u);
			if (!canSend) ImGui::BeginDisabled();
			if (ImGui::Button("Envoyer", ImVec2(120.f, 0.f)))
			{
				m_presenter->OpenTicket(m_bodyBuf);
				m_bodyBuf[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
			if (!canSend) ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Annuler", ImVec2(120.f, 0.f)))
			{
				m_presenter->CloseCompose();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}

#else // !_WIN32

namespace engine::render
{
	void GmTicketImGuiRenderer::Render()             {}
	void GmTicketImGuiRenderer::RenderListPanel()    {}
	void GmTicketImGuiRenderer::RenderComposeDialog(){}
}

#endif
