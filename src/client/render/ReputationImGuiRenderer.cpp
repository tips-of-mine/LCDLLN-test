// CMANGOS.24 (Phase 3.24 step 3+4) — ReputationImGuiRenderer implementation.

#include "src/client/render/ReputationImGuiRenderer.h"

#include "src/client/reputation/ReputationUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

		/// Bornes par standing (synchronise avec ReputationManager::StandingFor).
		/// Retourne la valeur min de l'intervalle pour le standing donne.
		int32_t StandingMin(int8_t standing)
		{
			switch (standing)
			{
			case -6: return -42000;  // Hated
			case -5: return -6000;   // Hostile
			case -4: return -3000;   // Unfriendly
			case -3: return -42000;  // Neutral (couvre tout l'intervalle <= 0 a partir de -3000)
			case -2: return 0;       // Friendly
			case -1: return 3000;    // Honored
			case 0:  return 9000;    // Revered
			case 1:  return 21000;   // Exalted
			default: return -42000;
			}
		}

		/// Bornes par standing : valeur max de l'intervalle.
		int32_t StandingMax(int8_t standing)
		{
			switch (standing)
			{
			case -6: return -42001; // pas de progress dans Hated (clamp min)
			case -5: return -3001;  // Hostile : -6000..-3001
			case -4: return -1;     // Unfriendly : -3000..-1
			case -3: return 0;      // Neutral : ..0
			case -2: return 3000;   // Friendly : 0..3000
			case -1: return 9000;   // Honored : 3000..9000
			case 0:  return 21000;  // Revered : 9000..21000
			case 1:  return 41999;  // Exalted : 21000..41999
			default: return 41999;
			}
		}

		/// Libelle FR pour un standing.
		const char* StandingLabel(int8_t standing)
		{
			switch (standing)
			{
			case -6: return "Haine";
			case -5: return "Hostile";
			case -4: return "Inamical";
			case -3: return "Neutre";
			case -2: return "Amical";
			case -1: return "Honore";
			case 0:  return "Revere";
			case 1:  return "Exalte";
			default: return "?";
			}
		}

		/// Couleur du texte / barre selon le standing.
		ImVec4 StandingColor(int8_t standing)
		{
			switch (standing)
			{
			case -6: return ImVec4(0.85f, 0.18f, 0.18f, 1.0f); // Hated rouge fonce
			case -5: return ImVec4(0.95f, 0.30f, 0.20f, 1.0f); // Hostile rouge
			case -4: return ImVec4(0.95f, 0.55f, 0.20f, 1.0f); // Unfriendly orange
			case -3: return ImVec4(0.70f, 0.70f, 0.70f, 1.0f); // Neutral gris
			case -2: return ImVec4(0.40f, 0.85f, 0.40f, 1.0f); // Friendly vert clair
			case -1: return ImVec4(0.30f, 0.90f, 0.30f, 1.0f); // Honored vert
			case 0:  return ImVec4(0.95f, 0.78f, 0.30f, 1.0f); // Revered or
			case 1:  return ImVec4(1.00f, 0.85f, 0.20f, 1.0f); // Exalted or vif
			default: return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
			}
		}

		/// Construit un nom de faction par defaut.
		/// V1 : pas de map id->name client (ticket FactionTemplate fournira la
		/// localisation ulterieurement). Hardcode "Faction #N".
		std::string FactionDisplayName(uint32_t factionId)
		{
			char buf[32]{};
			std::snprintf(buf, sizeof(buf), "Faction #%u", static_cast<unsigned>(factionId));
			return buf;
		}

		/// Calcule le ratio [0..1] du progress dans le standing courant.
		float StandingProgressRatio(int32_t value, int8_t standing)
		{
			const int32_t lo = StandingMin(standing);
			const int32_t hi = StandingMax(standing);
			if (hi <= lo)
				return 1.0f;
			const float ratio = static_cast<float>(value - lo)
				/ static_cast<float>(hi - lo);
			return std::clamp(ratio, 0.0f, 1.0f);
		}
	}

	void ReputationImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		RenderListPanel();
		RenderToast();
	}

	void ReputationImGuiRenderer::RenderListPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 350x500.
		const float panelW = 350.f;
		const float panelH = 500.f;
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
		if (ImGui::Begin("Reputation##ln_reputation_panel", nullptr, flags))
		{
			// Erreur transitoire (rouge) — non bloquante.
			if (!state.lastErrorText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastErrorText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			if (ImGui::Button("Rafraichir"))
			{
				m_presenter->RequestReputationList();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%zu faction%s)",
				state.entries.size(), state.entries.size() > 1 ? "s" : "");

			ImGui::Separator();

			if (state.isLoading)
			{
				ImGui::TextUnformatted("Chargement...");
			}
			else if (state.entries.empty())
			{
				ImGui::TextDisabled("Aucune reputation enregistree.");
			}
			else
			{
				const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
					| ImGuiTableFlags_RowBg
					| ImGuiTableFlags_ScrollY
					| ImGuiTableFlags_Resizable;
				if (ImGui::BeginTable("##ln_reputation_list", 4, tableFlags, ImVec2(0.f, 0.f)))
				{
					ImGui::TableSetupColumn("Faction",  ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Standing", ImGuiTableColumnFlags_WidthFixed,  85.f);
					ImGui::TableSetupColumn("Valeur",   ImGuiTableColumnFlags_WidthFixed,  60.f);
					ImGui::TableSetupColumn("Progres",  ImGuiTableColumnFlags_WidthFixed,  80.f);
					ImGui::TableHeadersRow();

					for (const auto& e : state.entries)
					{
						ImGui::TableNextRow();
						ImGui::PushID(static_cast<int>(e.factionId));

						ImGui::TableSetColumnIndex(0);
						const std::string name = FactionDisplayName(e.factionId);
						ImGui::TextUnformatted(name.c_str());

						ImGui::TableSetColumnIndex(1);
						const ImVec4 col = StandingColor(e.standing);
						ImGui::PushStyleColor(ImGuiCol_Text, col);
						ImGui::TextUnformatted(StandingLabel(e.standing));
						ImGui::PopStyleColor();

						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%d", e.value);

						ImGui::TableSetColumnIndex(3);
						const float ratio = StandingProgressRatio(e.value, e.standing);
						ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
						ImGui::ProgressBar(ratio, ImVec2(-FLT_MIN, 0.f), "");
						ImGui::PopStyleColor();

						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void ReputationImGuiRenderer::RenderToast()
	{
		const auto& state = m_presenter->GetState();
		if (!state.lastUpdateToast.has_value())
			return;

		const auto& upd = *state.lastUpdateToast;
		const std::string name = FactionDisplayName(upd.factionId);
		const char*       lab  = StandingLabel(upd.standing);
		const ImVec4      col  = StandingColor(upd.standing);

		// Toast en haut centre-droit.
		const float toastW = 320.f;
		const float toastH = 70.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float posX = std::max(0.f, vpW - toastW - margin);
		const float posY = margin;

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(toastW, toastH), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.85f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.85f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoInputs;
		if (ImGui::Begin("##ln_reputation_toast", nullptr, flags))
		{
			const int32_t delta = state.lastUpdateToastDelta;
			char buf[160]{};
			if (delta >= 0)
				std::snprintf(buf, sizeof(buf), "+%d %s", delta, name.c_str());
			else
				std::snprintf(buf, sizeof(buf), "%d %s", delta, name.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::TextUnformatted(buf);
			ImGui::PopStyleColor();

			std::snprintf(buf, sizeof(buf), "-> %s (%d)", lab, upd.value);
			ImGui::TextUnformatted(buf);
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void ReputationImGuiRenderer::Render()           {}
	void ReputationImGuiRenderer::RenderListPanel()  {}
	void ReputationImGuiRenderer::RenderToast()      {}
}

#endif
