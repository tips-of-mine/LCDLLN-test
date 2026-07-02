#include "src/client/render/MailImGuiRenderer.h"

#include "src/client/mail/MailUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

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
		std::string FormatRelativeTime(uint64_t sentTsMs)
		{
			if (sentTsMs == 0)
				return "—";
			using namespace std::chrono;
			const uint64_t nowMs = static_cast<uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
			if (sentTsMs > nowMs)
				return "a l'instant";
			const uint64_t deltaMs = nowMs - sentTsMs;
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

	void MailImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		RenderInboxPanel();
		RenderComposeDialog();
	}

	void MailImGuiRenderer::RenderInboxPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre bottom-right (taille 600x400 par defaut).
		const float panelW = 600.f;
		const float panelH = 400.f;
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
		if (ImGui::Begin("Boite mail##ln_mail_panel", nullptr, flags))
		{
			// Erreur transitoire (rouge) — non bloquante.
			if (!state.lastErrorText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastErrorText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			// Toolbar : Nouveau message + Refresh.
			if (ImGui::Button("Nouveau message"))
			{
				m_presenter->OpenCompose();
			}
			ImGui::SameLine();
			if (ImGui::Button("Rafraichir"))
			{
				m_presenter->RequestInbox();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%zu mail%s)", state.inbox.size(), state.inbox.size() > 1 ? "s" : "");

			ImGui::Separator();

			if (state.isInboxLoading)
			{
				ImGui::TextUnformatted("Chargement...");
			}
			else if (state.inbox.empty())
			{
				ImGui::TextDisabled("Aucun mail.");
			}
			else
			{
				// Tableau inbox.
				const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
					| ImGuiTableFlags_RowBg
					| ImGuiTableFlags_ScrollY
					| ImGuiTableFlags_Resizable;
				const float tableH = 180.f;
				if (ImGui::BeginTable("##ln_mail_inbox", 5, tableFlags, ImVec2(0.f, tableH)))
				{
					ImGui::TableSetupColumn("Expediteur", ImGuiTableColumnFlags_WidthFixed, 110.f);
					ImGui::TableSetupColumn("Sujet",      ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Recu",       ImGuiTableColumnFlags_WidthFixed, 90.f);
					ImGui::TableSetupColumn("Statut",     ImGuiTableColumnFlags_WidthFixed, 60.f);
					ImGui::TableSetupColumn("Actions",    ImGuiTableColumnFlags_WidthFixed, 120.f);
					ImGui::TableHeadersRow();

					for (const auto& m : state.inbox)
					{
						ImGui::TableNextRow();
						ImGui::PushID(static_cast<int>(m.mailId));

						ImGui::TableSetColumnIndex(0);
						char senderBuf[64];
						std::snprintf(senderBuf, sizeof(senderBuf), "Account #%" PRIu64, m.senderAccountId);
						ImGui::TextUnformatted(senderBuf);

						ImGui::TableSetColumnIndex(1);
						{
							const bool selected = state.selectedMailId.has_value()
								&& *state.selectedMailId == m.mailId;
							const std::string label = m.subject.empty() ? std::string("(sans sujet)") : m.subject;
							if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
							{
								m_presenter->SelectMail(m.mailId);
							}
						}

						ImGui::TableSetColumnIndex(2);
						ImGui::TextUnformatted(FormatRelativeTime(m.sentTsMs).c_str());

						ImGui::TableSetColumnIndex(3);
						ImGui::TextUnformatted(m.state == 0 ? "Non lu" : "Lu");

						ImGui::TableSetColumnIndex(4);
						const bool hasAttach = (m.copperGold > 0) || (m.copperCod > 0);
						if (hasAttach)
						{
							if (ImGui::SmallButton("Prendre"))
							{
								m_presenter->TakeAttachments(m.mailId);
							}
							ImGui::SameLine();
						}
						if (ImGui::SmallButton("Supprimer"))
						{
							m_presenter->DeleteMail(m.mailId);
						}

						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}

			// Panneau detail : sujet / corps du mail selectionne.
			if (state.selectedMailId.has_value())
			{
				const uint64_t selId = *state.selectedMailId;
				auto it = std::find_if(state.inbox.begin(), state.inbox.end(),
					[selId](const engine::client::MailUiInboxEntry& e) { return e.mailId == selId; });
				if (it != state.inbox.end())
				{
					ImGui::Separator();
					char header[256];
					std::snprintf(header, sizeof(header), "De Account #%" PRIu64 " — %s",
						it->senderAccountId,
						it->subject.empty() ? "(sans sujet)" : it->subject.c_str());
					ImGui::TextWrapped("%s", header);
					ImGui::TextDisabled("Recu %s", FormatRelativeTime(it->sentTsMs).c_str());

					ImGui::Spacing();
					if (it->bodyLoaded)
					{
						ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(LnTheme::kSurface));
						ImGui::BeginChild("##ln_mail_body", ImVec2(0.f, 100.f), true,
							ImGuiWindowFlags_HorizontalScrollbar);
						ImGui::TextWrapped("%s", it->body.c_str());
						ImGui::EndChild();
						ImGui::PopStyleColor();
					}
					else
					{
						ImGui::TextDisabled("Chargement du corps...");
					}

					ImGui::Spacing();
					if ((it->copperGold > 0) || (it->copperCod > 0))
					{
						if (ImGui::Button("Prendre les pieces jointes"))
						{
							m_presenter->TakeAttachments(it->mailId);
						}
						ImGui::SameLine();
					}
					if (ImGui::Button("Supprimer ce mail"))
					{
						m_presenter->DeleteMail(it->mailId);
					}
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void MailImGuiRenderer::RenderComposeDialog()
	{
		const auto& state = m_presenter->GetState();

		// Synchro buffers ImGui <- presenter quand la dialog s'ouvre.
		// Une fois ouverte, on laisse ImGui controller les buffers (push -> presenter via setters
		// chaque frame) sans re-charger l'etat depuis le presenter sinon on ecraserait la saisie.
		static bool s_wasOpen = false;
		if (state.isComposeOpen && !s_wasOpen)
		{
			LoadStringIntoBuffer(state.composeRecipient, m_recipientBuf, sizeof(m_recipientBuf));
			LoadStringIntoBuffer(state.composeSubject,   m_subjectBuf,   sizeof(m_subjectBuf));
			LoadStringIntoBuffer(state.composeBody,      m_bodyBuf,      sizeof(m_bodyBuf));
			ImGui::OpenPopup("Composer un mail##ln_mail_compose");
		}
		s_wasOpen = state.isComposeOpen;

		// Centre la fenetre modale.
		ImVec2 center;
		center.x = (m_viewportW > 0) ? static_cast<float>(m_viewportW) * 0.5f : 640.f;
		center.y = (m_viewportH > 0) ? static_cast<float>(m_viewportH) * 0.5f : 360.f;
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(520.f, 420.f), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal("Composer un mail##ln_mail_compose", nullptr,
			ImGuiWindowFlags_NoSavedSettings))
		{
			// Destinataire (account_id direct pour V1 ; resolution par login a venir).
			if (ImGui::InputText("Destinataire (account_id)", m_recipientBuf, sizeof(m_recipientBuf)))
			{
				m_presenter->SetComposeRecipient(m_recipientBuf);
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Saisir l'account_id numerique du destinataire (V1).");

			if (ImGui::InputText("Sujet", m_subjectBuf, sizeof(m_subjectBuf)))
			{
				m_presenter->SetComposeSubject(m_subjectBuf);
			}

			if (ImGui::InputTextMultiline("Corps", m_bodyBuf, sizeof(m_bodyBuf),
				ImVec2(-FLT_MIN, 180.f)))
			{
				m_presenter->SetComposeBody(m_bodyBuf);
			}

			// Or attache + COD : input scalar U64.
			uint64_t goldVal = state.composeGold;
			if (ImGui::InputScalar("Or (copper)", ImGuiDataType_U64, &goldVal))
			{
				m_presenter->SetComposeGold(goldVal);
			}
			uint64_t codVal = state.composeCod;
			if (ImGui::InputScalar("COD (copper)", ImGuiDataType_U64, &codVal))
			{
				m_presenter->SetComposeCod(codVal);
			}

			ImGui::Separator();
			if (ImGui::Button("Envoyer", ImVec2(120.f, 0.f)))
			{
				m_presenter->SubmitCompose();
				ImGui::CloseCurrentPopup();
			}
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
	void MailImGuiRenderer::Render()                  {}
	void MailImGuiRenderer::RenderInboxPanel()        {}
	void MailImGuiRenderer::RenderComposeDialog()     {}
}

#endif
