// CMANGOS.10 (Phase 5 step 3+4) — BattleGroundImGuiRenderer implementation.

#include "src/client/render/BattleGroundImGuiRenderer.h"

#include "src/client/battleground/BattleGroundUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;

		/// Libelle FR pour une faction.
		const char* FactionLabel(uint8_t fac)
		{
			switch (fac)
			{
			case 0: return "Lumiere";
			case 1: return "Lune Noire";
			case 2: return "Egalite";
			default: return "?";
			}
		}

		/// Format MM:SS pour un nombre de secondes.
		void FormatMmSs(uint32_t sec, char* out, size_t outSize)
		{
			const uint32_t m = sec / 60u;
			const uint32_t s = sec % 60u;
			std::snprintf(out, outSize, "%02u:%02u",
				static_cast<unsigned>(m), static_cast<unsigned>(s));
		}
	}

	void BattleGroundImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		const auto& state = m_presenter->GetState();
		if (state.activeMatchId.has_value())
			RenderActiveMatchPanel();
		else
			RenderMainPanel();
	}

	void BattleGroundImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 420x500.
		const float panelW = 420.f;
		const float panelH = 500.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - panelW - margin);
		const float posY = std::max(0.f, (vpH - panelH) * 0.5f);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Champ de bataille (F9)##ln_bg_panel", nullptr, flags))
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

			// Resultat du dernier match (toast persistant tant que pas
			// d'autre info ; le state n'a pas de fade-out auto cote presenter — V1).
			if (state.lastMatchWinner.has_value())
			{
				const uint8_t winner = *state.lastMatchWinner;
				ImVec4 col;
				if (winner == 0u) col = ImVec4(0.4f, 0.7f, 1.f, 1.f);   // Alliance bleu
				else if (winner == 1u) col = ImVec4(1.f, 0.4f, 0.4f, 1.f); // Horde rouge
				else col = ImVec4(0.85f, 0.85f, 0.85f, 1.f);              // Draw
				ImGui::PushStyleColor(ImGuiCol_Text, col);
				char durBuf[16]{};
				FormatMmSs(state.lastMatchDurationSec, durBuf, sizeof(durBuf));
				char buf[200]{};
				if (winner == 2u)
				{
					std::snprintf(buf, sizeof(buf), "Egalite ! %u-%u (%s)",
						static_cast<unsigned>(state.lastMatchAllianceScore),
						static_cast<unsigned>(state.lastMatchHordeScore),
						durBuf);
				}
				else
				{
					std::snprintf(buf, sizeof(buf), "Victoire %s ! %u-%u (%s)",
						FactionLabel(winner),
						static_cast<unsigned>(state.lastMatchAllianceScore),
						static_cast<unsigned>(state.lastMatchHordeScore),
						durBuf);
				}
				ImGui::TextWrapped("%s", buf);
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			if (state.inQueue)
			{
				ImGui::TextUnformatted("En queue BG :");
				ImGui::Separator();
				ImGui::Text("BG       : #%u", static_cast<unsigned>(state.queuedBgType));
				ImGui::Text("Faction  : %s", FactionLabel(state.queuedFaction));
				if (state.queuePosition > 0u)
					ImGui::Text("Position : %u", static_cast<unsigned>(state.queuePosition));
				if (state.estimatedWaitSec > 0u)
					ImGui::Text("Estime   : %u s", static_cast<unsigned>(state.estimatedWaitSec));
				ImGui::Spacing();
				if (ImGui::Button("Quitter la queue", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->LeaveQueue();
			}
			else
			{
				// Liste des BG + boutons Queue Alliance / Queue Horde par BG.
				if (!state.listLoaded || state.battlegrounds.empty())
				{
					if (ImGui::Button("Charger les BG", ImVec2(-FLT_MIN, 28.f)))
						m_presenter->RequestList();
					if (state.listLoaded && state.battlegrounds.empty())
					{
						ImGui::TextWrapped("Aucun champ de bataille disponible.");
					}
				}
				else
				{
					ImGui::TextUnformatted("Champs de bataille :");
					ImGui::Separator();
					const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
						| ImGuiTableFlags_RowBg
						| ImGuiTableFlags_ScrollY;
					if (ImGui::BeginTable("##ln_bg_list", 4, tableFlags, ImVec2(0.f, 280.f)))
					{
						ImGui::TableSetupColumn("Nom",       ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Carte",     ImGuiTableColumnFlags_WidthFixed, 80.f);
						ImGui::TableSetupColumn("Taille",    ImGuiTableColumnFlags_WidthFixed, 50.f);
						ImGui::TableSetupColumn("Faction",   ImGuiTableColumnFlags_WidthFixed, 160.f);
						ImGui::TableHeadersRow();
						for (const auto& bg : state.battlegrounds)
						{
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TextUnformatted(bg.name.c_str());
							ImGui::TableSetColumnIndex(1);
							ImGui::TextUnformatted(bg.mapName.c_str());
							ImGui::TableSetColumnIndex(2);
							ImGui::Text("%uv%u",
								static_cast<unsigned>(bg.teamSize),
								static_cast<unsigned>(bg.teamSize));
							ImGui::TableSetColumnIndex(3);
							ImGui::PushID(static_cast<int>(bg.bgType));
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.70f, 1.f));
							if (ImGui::Button("Lumiere", ImVec2(80.f, 22.f)))
								m_presenter->Queue(bg.bgType, 0u);
							ImGui::PopStyleColor();
							ImGui::SameLine();
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 1.f));
							if (ImGui::Button("Lune Noire", ImVec2(90.f, 22.f)))
								m_presenter->Queue(bg.bgType, 1u);
							ImGui::PopStyleColor();
							ImGui::PopID();
						}
						ImGui::EndTable();
					}
					ImGui::Spacing();
					if (ImGui::Button("Rafraichir la liste", ImVec2(-FLT_MIN, 28.f)))
						m_presenter->RequestList();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void BattleGroundImGuiRenderer::RenderActiveMatchPanel()
	{
		const auto& state = m_presenter->GetState();
		if (!state.activeMatchId.has_value())
			return;

		// Geometrie : scoreboard centre haut, 520x180.
		const float panelW = 520.f;
		const float panelH = 180.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, (vpW - panelW) * 0.5f);
		const float posY = 24.f;
		(void)vpH;

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings;
		if (ImGui::Begin("Champ de bataille en cours##ln_bg_active", nullptr, flags))
		{
			char durBuf[16]{};
			FormatMmSs(state.matchElapsedSec, durBuf, sizeof(durBuf));

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.20f, 1.f));
			ImGui::Text("%s (BG #%u) - %s",
				state.activeMatchMap.c_str(),
				static_cast<unsigned>(state.activeMatchBgType),
				durBuf);
			ImGui::PopStyleColor();
			ImGui::Separator();

			// Scoreboard "Alliance X vs Horde Y" avec couleurs.
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.f, 1.f));
			ImGui::Text("Lumiere    : %u (%u joueurs)",
				static_cast<unsigned>(state.allianceScore),
				static_cast<unsigned>(state.activeAllianceCount));
			ImGui::PopStyleColor();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
			ImGui::Text("Lune Noire : %u (%u joueurs)",
				static_cast<unsigned>(state.hordeScore),
				static_cast<unsigned>(state.activeHordeCount));
			ImGui::PopStyleColor();

			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.f));
			if (ImGui::Button("Forfait", ImVec2(-FLT_MIN, 28.f)))
				m_presenter->LeaveMatch();
			ImGui::PopStyleColor();
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void BattleGroundImGuiRenderer::Render()                  {}
	void BattleGroundImGuiRenderer::RenderMainPanel()         {}
	void BattleGroundImGuiRenderer::RenderActiveMatchPanel()  {}
}

#endif
