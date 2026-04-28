// Phase 2 — Rendu ImGui de l'écran "Choisir un personnage".
// Affiche la liste des personnages reçus du master via CHARACTER_LIST (opcode 39) après TICKET_ACCEPTED,
// puis 3 actions : Jouer (perso sélectionné), Créer un nouveau personnage, Retour.

#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/LnTheme.h"

#include "engine/client/LocalizationService.h"
#include "engine/network/CharacterPayloads.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}
	}

	void AuthImGuiRenderer::RenderCharacterSelectScreen(const RenderModel& rm, float vpW, float vpH)
	{
		using P = engine::client::LocalizationService::Params;
		auto tr = [this](const char* key, const P& p = {}) -> std::string {
			if (m_authPresenter == nullptr)
				return std::string(key);
			std::string s = m_authPresenter->UiTranslate(key, p);
			return s.empty() ? std::string(key) : s;
		};

		// Stage title (cohérence avec Login / Register / CharacterCreate) : le grand titre
		// « LES CHRONIQUES » et son sous-titre sont dessinés AU-DESSUS du cadre. Le cadre
		// ne porte que le titre de section (« Choisir un personnage »).
		const std::string& h1 = rm.titleLine1.empty() ? std::string("Les Chroniques de la Lune Noire") : rm.titleLine1;
		ImGui::SetWindowFontScale(2.4f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(h1.c_str()).x;
		ImGui::SetCursorPos(ImVec2((vpW - w1) * 0.5f, vpH * 0.05f));
		ImGui::TextUnformatted(h1.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		if (!rm.titleLine2.empty())
		{
			ImGui::SetWindowFontScale(1.5f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			const float w2 = ImGui::CalcTextSize(rm.titleLine2.c_str()).x;
			ImGui::SetCursorPos(ImVec2((vpW - w2) * 0.5f, ImGui::GetCursorPosY() + 2.f));
			ImGui::TextUnformatted(rm.titleLine2.c_str());
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.f);
		}

		const std::string titleStr = rm.sectionTitle.empty()
			? tr("auth.character_select.panel_title") : rm.sectionTitle;
		const std::string subStr = tr("auth.character_select.panel_subtitle");

		static const std::vector<engine::network::CharacterListEntry> kEmpty{};
		const auto& entries = m_authPresenter
			? m_authPresenter->CharacterListEntries() : kEmpty;
		const int selected = m_authPresenter ? m_authPresenter->CharacterSelectIndex() : -1;

		if (!BeginPanel(680.f, vpW, vpH, std::string_view(titleStr), std::string_view(subStr), std::string_view(""), true, false))
		{
			EndPanel();
			return;
		}

		const std::string hintStr = tr("auth.character_select.hint");
		if (!hintStr.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.85f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 620.f);
			ImGui::TextWrapped("%s", hintStr.c_str());
			ImGui::PopTextWrapPos();
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		const float listH = (std::max)(180.f, vpH * 0.38f);
		ImGui::BeginChild("##charselect_scroll", ImVec2(-FLT_MIN, listH), true, ImGuiWindowFlags_None);

		if (entries.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", tr("auth.character_select.empty").c_str());
			ImGui::PopStyleColor();
		}
		else
		{
			constexpr float kRowH = 64.f;
			for (size_t i = 0; i < entries.size(); ++i)
			{
				const auto& c = entries[i];
				const bool isSelected = (static_cast<int>(i) == selected);

				ImGui::PushStyleColor(ImGuiCol_ChildBg,
					isSelected ? IV(LnTheme::AccentDim(0.1f)) : IV(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border,
					isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);
				char rowId[40];
				std::snprintf(rowId, sizeof(rowId), "##charsel%zu", i);
				ImGui::BeginChild(rowId, ImVec2(-FLT_MIN, kRowH), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);

				const std::string nameUpper = c.name.empty() ? std::string("?") : c.name;
				ImGui::PushStyleColor(ImGuiCol_Text,
					isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
				ImGui::SetWindowFontScale(1.05f);
				ImGui::TextUnformatted(nameUpper.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();

				const std::string sub = tr("auth.character_select.subline",
					P{ { "slot", std::to_string(static_cast<unsigned>(c.slot) + 1u) },
					   { "level", std::to_string(c.level) } });
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				ImGui::TextUnformatted(sub.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();

				ImGui::SetCursorPos(ImVec2(0.f, 0.f));
				char invId[48];
				std::snprintf(invId, sizeof(invId), "##charinv%zu", i);
				ImGui::InvisibleButton(invId, ImVec2(ImGui::GetWindowWidth() - 8.f, kRowH));
				if (ImGui::IsItemClicked() && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiSelectCharacterEntry(static_cast<int>(i));
				}

				ImGui::EndChild();
				ImGui::Spacing();
			}
		}

		ImGui::EndChild();
		ImGui::Spacing();

		// 3 boutons : Retour | Créer nouveau | Jouer
		const bool canPlay = (selected >= 0
			&& static_cast<size_t>(selected) < entries.size());
		const bool canCreate = (entries.size() < 5u);

		const float backW = 132.f;
		const float createW = 220.f;
		const float playW = 188.f;

		auto drawGhost = [&](const char* label, float w, bool disabled) -> bool {
			if (disabled)
				ImGui::BeginDisabled();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.15f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			const bool clicked = ImGui::Button(label, ImVec2(w, 32.f));
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			if (disabled)
				ImGui::EndDisabled();
			return clicked;
		};

		const std::string backStr = tr("common.back");
		const std::string createStr = tr("auth.character_select.create_new");
		const std::string playStr = tr("auth.character_select.play");

		if (drawGhost(backStr.c_str(), backW, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCancelCharacterSelectReturnToLogin();
		}
		ImGui::SameLine(0.f, 12.f);
		if (drawGhost(createStr.c_str(), createW, !canCreate) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCreateAnotherCharacterFromSelect();
		}
		ImGui::SameLine(0.f, 0.f);
		ImGui::Dummy(ImVec2((std::max)(0.f, ImGui::GetContentRegionAvail().x - playW - 4.f), 1.f));
		ImGui::SameLine(0.f, 0.f);
		if (!canPlay)
			ImGui::BeginDisabled();
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.25f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.35f)));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		const bool playClick = ImGui::Button(playStr.c_str(), ImVec2(playW, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (!canPlay)
			ImGui::EndDisabled();
		if (playClick && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiActivateSelectedCharacter();
		}

		EndPanel();
	}
} // namespace engine::render

#endif
