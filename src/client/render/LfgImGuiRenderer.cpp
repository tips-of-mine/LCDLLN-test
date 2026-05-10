// CMANGOS.33 (Phase 5.33 step 3+4) — LfgImGuiRenderer implementation.

#include "src/client/render/LfgImGuiRenderer.h"

#include "src/client/lfg/LfgUi.h"
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

		/// V1 : catalogue dungeon hardcode cote client. Le master n'a pas de
		/// catalogue centralise (tout dungeonId > 0 est accepte). Le ticket
		/// futur DungeonCatalog cote shared/ permettra d'unifier.
		struct DungeonInfo
		{
			uint32_t    id;
			const char* label;
		};
		constexpr DungeonInfo kV1Dungeons[] = {
			{1u, "Cave"},
			{2u, "Tower"},
			{3u, "Crypte"},
		};

		/// Libelle FR pour un role.
		const char* RoleLabel(uint8_t role)
		{
			switch (role)
			{
			case 0: return "Tank";
			case 1: return "Soigneur";
			case 2: return "Dommages";
			default: return "?";
			}
		}

		/// Libelle FR pour un dungeon (lookup dans le catalogue ; sinon "Donjon #N").
		std::string DungeonLabel(uint32_t dungeonId)
		{
			for (const auto& d : kV1Dungeons)
			{
				if (d.id == dungeonId) return std::string(d.label);
			}
			char buf[32]{};
			std::snprintf(buf, sizeof(buf), "Donjon #%u", static_cast<unsigned>(dungeonId));
			return buf;
		}
	}

	void LfgImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		RenderMainPanel();
		RenderProposalModal();
	}

	void LfgImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 320x420.
		const float panelW = 320.f;
		const float panelH = 420.f;
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
		if (ImGui::Begin("LFG##ln_lfg_panel", nullptr, flags))
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

			if (state.inQueue)
			{
				// Affichage etat de queue.
				ImGui::TextUnformatted("En queue :");
				ImGui::Separator();
				ImGui::Text("Role     : %s", RoleLabel(state.myRole));
				ImGui::Text("Donjon   : %s", DungeonLabel(state.myDungeonId).c_str());
				ImGui::Text("Ecoule   : %u s", static_cast<unsigned>(state.elapsedSec));
				if (state.estimatedWaitSec > 0u)
					ImGui::Text("Estime   : %u s", static_cast<unsigned>(state.estimatedWaitSec));
				ImGui::Spacing();

				if (ImGui::Button("Rafraichir", ImVec2(140.f, 28.f)))
					m_presenter->RequestStatus();
				ImGui::SameLine();
				if (ImGui::Button("Quitter la queue", ImVec2(140.f, 28.f)))
					m_presenter->RequestLeave();
			}
			else
			{
				// Selecteur de role + dungeon + bouton Queue.
				ImGui::TextUnformatted("Role :");
				ImGui::RadioButton("Tank",     &m_uiSelectedRoleIdx, 0);
				ImGui::SameLine();
				ImGui::RadioButton("Soigneur", &m_uiSelectedRoleIdx, 1);
				ImGui::SameLine();
				ImGui::RadioButton("Dommages", &m_uiSelectedRoleIdx, 2);
				ImGui::Separator();

				ImGui::TextUnformatted("Donjon :");
				const std::string previewLabel = DungeonLabel(m_uiSelectedDungeonId);
				if (ImGui::BeginCombo("##ln_lfg_dungeon_combo", previewLabel.c_str()))
				{
					for (const auto& d : kV1Dungeons)
					{
						const bool selected = (d.id == m_uiSelectedDungeonId);
						if (ImGui::Selectable(d.label, selected))
						{
							m_uiSelectedDungeonId = d.id;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				ImGui::Separator();

				if (ImGui::Button("S'inscrire dans la queue", ImVec2(-FLT_MIN, 32.f)))
				{
					const uint8_t role = static_cast<uint8_t>(m_uiSelectedRoleIdx);
					m_presenter->RequestQueue(role, m_uiSelectedDungeonId);
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void LfgImGuiRenderer::RenderProposalModal()
	{
		const auto& state = m_presenter->GetState();
		if (!state.hasProposal)
			return;

		// Modal centre, taille fixe 480x320.
		const float modalW = 480.f;
		const float modalH = 320.f;
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
		if (ImGui::Begin("Donjon trouve !##ln_lfg_proposal", nullptr, flags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.20f, 1.f));
			ImGui::TextUnformatted(DungeonLabel(state.proposalDungeonId).c_str());
			ImGui::PopStyleColor();
			ImGui::Separator();

			ImGui::TextUnformatted("Membres du groupe :");
			const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
				| ImGuiTableFlags_RowBg
				| ImGuiTableFlags_ScrollY;
			if (ImGui::BeginTable("##ln_lfg_proposal_members", 2, tableFlags, ImVec2(0.f, 180.f)))
			{
				ImGui::TableSetupColumn("Account", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Role",    ImGuiTableColumnFlags_WidthFixed, 100.f);
				ImGui::TableHeadersRow();
				for (const auto& [accId, role] : state.proposalMembers)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("#%llu", static_cast<unsigned long long>(accId));
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(RoleLabel(role));
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();

			const float btnW = (modalW - 60.f) * 0.5f;
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.20f, 1.f));
			if (ImGui::Button("Accepter", ImVec2(btnW, 32.f)))
			{
				m_presenter->AcceptMatch(state.proposalId, true);
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.f));
			if (ImGui::Button("Refuser", ImVec2(btnW, 32.f)))
			{
				m_presenter->AcceptMatch(state.proposalId, false);
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
	void LfgImGuiRenderer::Render()             {}
	void LfgImGuiRenderer::RenderMainPanel()    {}
	void LfgImGuiRenderer::RenderProposalModal(){}
}

#endif
