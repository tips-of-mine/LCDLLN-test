// CMANGOS.21 (Phase 5.21 step 3+4) — ArenaImGuiRenderer implementation.

#include "src/client/render/ArenaImGuiRenderer.h"

#include "src/client/arena/ArenaUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

		/// Libelle FR pour une taille d'arene.
		const char* SizeLabel(uint8_t size)
		{
			switch (size)
			{
			case 2: return "2v2";
			case 3: return "3v3";
			case 5: return "5v5";
			default: return "?";
			}
		}
	}

	void ArenaImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		RenderMainPanel();
		RenderProposalPopup();
	}

	void ArenaImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 380x460.
		const float panelW = 380.f;
		const float panelH = 460.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - panelW - margin);
		const float posY = std::max(0.f, (vpH - panelH) * 0.5f);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Arene (F8)##ln_arena_panel", nullptr, flags))
		{
			// Erreur transitoire (rouge).
			if (!state.lastErrorText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastErrorText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}
			// Info transitoire (vert leger).
			if (!state.lastInfoText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastInfoText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			// Resultat du dernier match (toast persistant tant que pas d'autre
			// info ; le state n'a pas de fade-out auto cote presenter — V1).
			if (state.lastMatchWin.has_value())
			{
				const bool win = *state.lastMatchWin;
				ImGui::PushStyleColor(ImGuiCol_Text,
					win ? ImVec4(0.4f, 1.f, 0.4f, 1.f)
					    : ImVec4(1.f, 0.4f, 0.4f, 1.f));
				char buf[160]{};
				std::snprintf(buf, sizeof(buf), "%s %+d rating contre %s",
					win ? "Victoire" : "Defaite",
					state.lastMatchRatingDelta,
					state.lastMatchOpponent.c_str());
				ImGui::TextWrapped("%s", buf);
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			if (state.inQueue)
			{
				ImGui::TextUnformatted("En queue arene :");
				ImGui::Separator();
				ImGui::Text("Equipe : #%u", static_cast<unsigned>(state.queuedTeamId));
				ImGui::Text("Mode   : %s", SizeLabel(state.queuedSize));
				if (state.estimatedWaitSec > 0u)
					ImGui::Text("Estime : %u s", static_cast<unsigned>(state.estimatedWaitSec));
				ImGui::Spacing();
				if (ImGui::Button("Quitter la queue", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->LeaveQueue();
			}
			else
			{
				// Liste des teams + bouton Queue par team.
				if (!state.teamsLoaded || state.teams.empty())
				{
					if (ImGui::Button("Charger mes equipes", ImVec2(-FLT_MIN, 28.f)))
						m_presenter->RequestTeams();
					if (state.teamsLoaded && state.teams.empty())
					{
						ImGui::TextWrapped("Aucune equipe arena.");
					}
				}
				else
				{
					ImGui::TextUnformatted("Mes equipes :");
					ImGui::Separator();
					const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
						| ImGuiTableFlags_RowBg
						| ImGuiTableFlags_ScrollY;
					if (ImGui::BeginTable("##ln_arena_teams", 5, tableFlags, ImVec2(0.f, 220.f)))
					{
						ImGui::TableSetupColumn("Nom",    ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Mode",   ImGuiTableColumnFlags_WidthFixed, 50.f);
						ImGui::TableSetupColumn("Cote",   ImGuiTableColumnFlags_WidthFixed, 60.f);
						ImGui::TableSetupColumn("V-D",    ImGuiTableColumnFlags_WidthFixed, 60.f);
						ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.f);
						ImGui::TableHeadersRow();
						for (const auto& t : state.teams)
						{
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TextUnformatted(t.name.c_str());
							ImGui::TableSetColumnIndex(1);
							ImGui::TextUnformatted(SizeLabel(t.size));
							ImGui::TableSetColumnIndex(2);
							ImGui::Text("%u", static_cast<unsigned>(t.rating));
							ImGui::TableSetColumnIndex(3);
							ImGui::Text("%u-%u",
								static_cast<unsigned>(t.weeklyWins),
								static_cast<unsigned>(
									t.weeklyGames > t.weeklyWins
										? (t.weeklyGames - t.weeklyWins) : 0u));
							ImGui::TableSetColumnIndex(4);
							ImGui::PushID(static_cast<int>(t.teamId));
							if (ImGui::Button("Rejoindre", ImVec2(70.f, 22.f)))
								m_presenter->Queue(t.teamId, t.size);
							ImGui::PopID();
						}
						ImGui::EndTable();
					}
					ImGui::Spacing();
					if (ImGui::Button("Rafraichir la liste", ImVec2(-FLT_MIN, 28.f)))
						m_presenter->RequestTeams();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void ArenaImGuiRenderer::RenderProposalPopup()
	{
		const auto& state = m_presenter->GetState();
		if (!state.pendingProposalId.has_value())
			return;

		// Modal centre, taille fixe 480x240.
		const float modalW = 480.f;
		const float modalH = 240.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, (vpW - modalW) * 0.5f);
		const float posY = std::max(0.f, (vpH - modalH) * 0.5f);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(modalW, modalH), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.97f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.97f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings;
		if (ImGui::Begin("Match arene trouve !##ln_arena_proposal", nullptr, flags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.20f, 1.f));
			ImGui::TextWrapped("Adversaire : %s", state.pendingOpponentName.c_str());
			ImGui::PopStyleColor();
			ImGui::Text("Cote : %u", static_cast<unsigned>(state.pendingOpponentRating));
			ImGui::Separator();

			ImGui::TextWrapped(
				"Vous allez affronter cette equipe en arene. "
				"Acceptez pour lancer le combat ou refusez pour annuler.");
			ImGui::Spacing();

			const float btnW = (modalW - 60.f) * 0.5f;
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.20f, 1.f));
			if (ImGui::Button("Accepter", ImVec2(btnW, 32.f)))
			{
				m_presenter->AcceptProposal(*state.pendingProposalId, true);
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.f));
			if (ImGui::Button("Refuser", ImVec2(btnW, 32.f)))
			{
				m_presenter->AcceptProposal(*state.pendingProposalId, false);
			}
			ImGui::PopStyleColor();
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void ArenaImGuiRenderer::Render()             {}
	void ArenaImGuiRenderer::RenderMainPanel()    {}
	void ArenaImGuiRenderer::RenderProposalPopup(){}
}

#endif
