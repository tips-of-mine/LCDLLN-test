// CMANGOS.39 (Phase 4.39 step 3+4) — SkillBookImGuiRenderer implementation.

#include "src/client/render/SkillBookImGuiRenderer.h"

#include "src/client/render/LnTheme.h"
#include "src/client/skills/SkillBookUi.h"

#include <algorithm>
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

		/// Couleur du texte / barre selon le ratio value/cap.
		ImVec4 SkillProgressColor(uint16_t value, uint16_t cap)
		{
			if (cap == 0u) return ImVec4(0.70f, 0.70f, 0.70f, 1.f);
			const float ratio = static_cast<float>(value) / static_cast<float>(cap);
			if (ratio >= 1.0f) return ImVec4(1.00f, 0.85f, 0.20f, 1.f); // or = capped
			if (ratio >= 0.75f) return ImVec4(0.40f, 0.85f, 0.40f, 1.f); // vert
			if (ratio >= 0.40f) return ImVec4(0.95f, 0.85f, 0.30f, 1.f); // jaune
			return ImVec4(0.95f, 0.55f, 0.20f, 1.f); // orange (debutant)
		}

		/// Libelle FR pour un SkillUseResult (0=Success, 1=Fail, 2=Crit).
		const char* UseResultLabel(uint8_t result)
		{
			switch (result)
			{
			case 0u: return "Succes";
			case 1u: return "Echec";
			case 2u: return "Critique";
			default: return "?";
			}
		}

		/// Couleur du libelle indicateur Use.
		ImVec4 UseResultColor(uint8_t result)
		{
			switch (result)
			{
			case 0u: return ImVec4(0.40f, 0.85f, 0.40f, 1.0f); // Success vert
			case 1u: return ImVec4(0.85f, 0.40f, 0.40f, 1.0f); // Fail rouge
			case 2u: return ImVec4(1.00f, 0.85f, 0.20f, 1.0f); // Crit or
			default: return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
			}
		}
	}

	void SkillBookImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		RenderListPanel();
		RenderUseIndicator();
	}

	void SkillBookImGuiRenderer::RenderListPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 380x520. Decale legerement vs
		// reputation pour eviter le chevauchement direct si les deux sont
		// ouverts en meme temps.
		const float panelW = 380.f;
		const float panelH = 520.f;
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
		if (ImGui::Begin("Livre de competences (F2)##ln_skillbook_panel", nullptr, flags))
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
				m_presenter->RequestList();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%zu competence%s)",
				state.skills.size(), state.skills.size() > 1 ? "s" : "");

			ImGui::Separator();

			if (state.isLoading)
			{
				ImGui::TextUnformatted("Chargement...");
			}
			else if (state.skills.empty())
			{
				ImGui::TextDisabled("Aucune competence apprise.");
			}
			else
			{
				const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
					| ImGuiTableFlags_RowBg
					| ImGuiTableFlags_ScrollY
					| ImGuiTableFlags_Resizable;
				if (ImGui::BeginTable("##ln_skillbook_list", 4, tableFlags, ImVec2(0.f, 0.f)))
				{
					ImGui::TableSetupColumn("Competence", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Valeur",     ImGuiTableColumnFlags_WidthFixed,  90.f);
					ImGui::TableSetupColumn("Progres",    ImGuiTableColumnFlags_WidthFixed,  90.f);
					ImGui::TableSetupColumn("",           ImGuiTableColumnFlags_WidthFixed,  60.f);
					ImGui::TableHeadersRow();

					for (const auto& e : state.skills)
					{
						ImGui::TableNextRow();
						ImGui::PushID(static_cast<int>(e.skillId));

						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(e.name.c_str());
						if (e.bonus > 0u)
						{
							ImGui::SameLine();
							ImGui::TextDisabled("(+%u)", static_cast<unsigned>(e.bonus));
						}

						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%u / %u", static_cast<unsigned>(e.value),
							static_cast<unsigned>(e.cap));

						ImGui::TableSetColumnIndex(2);
						const float ratio = (e.cap > 0u)
							? std::clamp(static_cast<float>(e.value) / static_cast<float>(e.cap), 0.f, 1.f)
							: 0.f;
						const ImVec4 col = SkillProgressColor(e.value, e.cap);
						ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
						ImGui::ProgressBar(ratio, ImVec2(-FLT_MIN, 0.f), "");
						ImGui::PopStyleColor();

						ImGui::TableSetColumnIndex(3);
						// Bouton Use desactive si value == cap (pas de progression
						// possible) ou si value == 0 (skill non encore appris reel ;
						// V1 : Lockpicking commence a 0).
						const bool capped = (e.cap > 0u && e.value >= e.cap);
						if (capped)
						{
							ImGui::BeginDisabled();
							ImGui::Button("Cap");
							ImGui::EndDisabled();
						}
						else
						{
							if (ImGui::Button("Utiliser"))
							{
								m_presenter->RequestUse(e.skillId, 0u);
							}
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

	void SkillBookImGuiRenderer::RenderUseIndicator()
	{
		const auto& state = m_presenter->GetState();
		if (!state.lastUseResult.has_value())
			return;

		const uint8_t result = *state.lastUseResult;
		const ImVec4 col = UseResultColor(result);
		const char*  lab = UseResultLabel(result);

		// Indicateur centre horizontalement, place SOUS la zone du cadre cible
		// (cadre cible : y ~56..126 haut-centre) pour ne plus le chevaucher.
		// Fade-out implicite (la fenetre disparait quand TickIndicator clear
		// lastUseResult).
		const float toastW = 280.f;
		const float toastH = 70.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float posX = std::max(0.f, (vpW - toastW) * 0.5f);
		const float posY = 140.f; // sous le cadre cible (qui se termine vers y=126).

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
		if (ImGui::Begin("##ln_skillbook_use_indicator", nullptr, flags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::TextUnformatted(lab);
			ImGui::PopStyleColor();

			char buf[160]{};
			if (state.lastUseSkillId != 0u)
			{
				const std::string name = engine::client::GetSkillName(state.lastUseSkillId);
				if (state.lastUseDelta > 0u)
					std::snprintf(buf, sizeof(buf), "+%u %s",
						static_cast<unsigned>(state.lastUseDelta), name.c_str());
				else
					std::snprintf(buf, sizeof(buf), "%s", name.c_str());
			}
			else if (state.lastUseDelta > 0u)
			{
				std::snprintf(buf, sizeof(buf), "+%u", static_cast<unsigned>(state.lastUseDelta));
			}
			else
			{
				std::snprintf(buf, sizeof(buf), "(pas de gain)");
			}
			ImGui::TextUnformatted(buf);
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void SkillBookImGuiRenderer::Render()             {}
	void SkillBookImGuiRenderer::RenderListPanel()    {}
	void SkillBookImGuiRenderer::RenderUseIndicator() {}
}

#endif
