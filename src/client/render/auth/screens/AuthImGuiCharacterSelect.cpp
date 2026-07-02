// Phase 2 - Rendu ImGui de l'ecran "Choisir un personnage".
// Affiche la liste des personnages recus du master via CHARACTER_LIST (opcode 39) apres TICKET_ACCEPTED,
// puis 3 actions : Jouer (perso selectionne), Creer un nouveau personnage, Retour.

#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/LnTheme.h"

#include "src/client/localization/LocalizationService.h"
#include "src/shared/network/CharacterPayloads.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;
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

		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "charselect");
		const float titleZoneW = vpW * 0.96f;

		const std::string titleStr = rm.sectionTitle.empty()
			? tr("auth.character_select.panel_title") : rm.sectionTitle;
		const std::string subStr = tr("auth.character_select.panel_subtitle");

		static const std::vector<engine::network::CharacterListEntry> kEmpty{};
		const auto& entries = m_authPresenter
			? m_authPresenter->CharacterListEntries() : kEmpty;
		const int selected = m_authPresenter ? m_authPresenter->CharacterSelectIndex() : -1;

		if (!BeginPanel(680.f, titleZoneW, vpH, std::string_view(titleStr), std::string_view(subStr), std::string_view(""), true, false))
		{
			EndPanel();
			ImGui::EndChild();
			return;
		}

		const std::string hintStr = tr("auth.character_select.hint");
		if (!hintStr.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
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
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
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
					isSelected ? ToImVec4(LnTheme::AccentDim(0.1f)) : ToImVec4(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border,
					isSelected ? ToImVec4(LnTheme::kAccent) : ToImVec4(LnTheme::kBorder));
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);
				char rowId[40];
				std::snprintf(rowId, sizeof(rowId), "##charsel%zu", i);
				ImGui::BeginChild(rowId, ImVec2(-FLT_MIN, kRowH), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);

				const std::string nameUpper = c.name.empty() ? std::string("?") : c.name;
				ImGui::PushStyleColor(ImGuiCol_Text,
					isSelected ? ToImVec4(LnTheme::kAccent) : ToImVec4(LnTheme::kText));
				ImGui::SetWindowFontScale(1.05f);
				ImGui::TextUnformatted(nameUpper.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();

				// Phase 3.8 - Affichage humanise race/classe :
				// - si race_str/class_str non-vides (perso post-migration 0033) on les utilise
				//   via une cle de localisation "auth.character_select.race.<id>" pour le label,
				//   en retombant sur l'identifiant brut si aucune traduction n'est definie.
				// - sinon (perso pre-migration), on cache la mention pour ne pas afficher
				//   "Race ?" qui est plus distrayant qu'utile.
				auto humanize = [&](const char* prefix, const std::string& id) -> std::string {
					if (id.empty())
						return std::string{};
					std::string key = prefix;
					key += id;
					std::string localized = m_authPresenter ? m_authPresenter->UiTranslate(key) : std::string{};
					// UiTranslate renvoie la cle elle-meme quand aucune traduction n'existe :
					// tester `!empty()` laissait donc passer la cle brute (ex.
					// "auth.character_select.class.guerrier") a l'ecran. On exige que la valeur
					// traduite differe de la cle, sinon on retombe sur la capitalisation de l'id.
					if (!localized.empty() && localized != key)
						return localized;
					// Capitalisation simple du premier caractere ASCII (lettre minuscule -> majuscule).
					std::string copy = id;
					if (!copy.empty() && copy[0] >= 'a' && copy[0] <= 'z')
						copy[0] = static_cast<char>(copy[0] - 'a' + 'A');
					return copy;
				};
				const std::string raceLabel  = humanize("auth.character_select.race.", c.race_str);
				const std::string classLabel = humanize("auth.character_select.class.", c.class_str);
				// Symbole d'identification rapide de la race : initiale ASCII en accent.
				// Aligne sur les ids strings de la table races (cf. migration 0036).
				const auto raceSymbol = [](const std::string& raceId) -> char {
					if (raceId == "humains")             return 'H';
					if (raceId == "elfes")               return 'E';
					if (raceId == "orcs")                return 'O';
					if (raceId == "nains")               return 'N';
					if (raceId == "demons")              return 'D';
					if (raceId == "chevaliers_dragons")  return 'C';
					return '\0';
				};
				const char raceSym = raceSymbol(c.race_str);

				// Compose la subline en code : permet de gommer cleanement les parties
				// race / classe quand elles sont vides (avant on affichait "?" en fallback,
				// ce qui parasitait l'UI : 'Slot 1 - Elfe ? - Niveau 1').
				std::string sub = "Slot " + std::to_string(static_cast<unsigned>(c.slot) + 1u);
				if (!raceLabel.empty() || !classLabel.empty())
				{
					sub += " - ";
					if (!raceLabel.empty())  sub += raceLabel;
					if (!raceLabel.empty() && !classLabel.empty()) sub += " ";
					if (!classLabel.empty()) sub += classLabel;
				}
				sub += " - Niveau " + std::to_string(c.level);

				// Symbole race (lettre) en accent juste avant la subline si dispo.
				if (raceSym != '\0')
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
					ImGui::SetWindowFontScale(0.95f);
					char symBuf[2] = { raceSym, '\0' };
					ImGui::TextUnformatted(symBuf);
					ImGui::SetWindowFontScale(1.f);
					ImGui::PopStyleColor();
					ImGui::SameLine(0.f, 8.f);
				}
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				ImGui::TextUnformatted(sub.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();

				// Phase 3.9 - Bouton suppression a droite de la ligne. Etats :
				// 1) normal      : bouton "X" - premier clic arme la confirmation.
				// 2) confirming  : "Confirmer ?" - second clic supprime ; clic ailleurs annule.
				const int pendingDeleteIdx = m_authPresenter
					? m_authPresenter->PendingDeleteCharacterIndex() : -1;
				const bool isConfirming = (pendingDeleteIdx == static_cast<int>(i));
				constexpr float kDeleteBtnW = 110.f;
				const float btnX = ImGui::GetWindowWidth() - kDeleteBtnW - 12.f;
				ImGui::SetCursorPos(ImVec2(btnX, (kRowH - 28.f) * 0.5f));
				if (isConfirming)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kErrorCol));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::kErrorCol));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::kErrorCol));
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
					char btnId[48];
					std::snprintf(btnId, sizeof(btnId), "%s##chardel_confirm%zu",
						tr("auth.character_select.delete_confirm").c_str(), i);
					if (ImGui::Button(btnId, ImVec2(kDeleteBtnW, 28.f))
						&& m_authPresenter != nullptr && m_authCfg != nullptr)
					{
						m_authPresenter->ImGuiRequestDeleteCharacter(static_cast<int>(i), *m_authCfg);
					}
					ImGui::PopStyleColor(4);
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.08f)));
					ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kErrorCol));
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kErrorCol));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
					char btnId[48];
					std::snprintf(btnId, sizeof(btnId), "%s##chardel%zu",
						tr("auth.character_select.delete").c_str(), i);
					if (ImGui::Button(btnId, ImVec2(kDeleteBtnW, 28.f))
						&& m_authPresenter != nullptr && m_authCfg != nullptr)
					{
						m_authPresenter->ImGuiRequestDeleteCharacter(static_cast<int>(i), *m_authCfg);
					}
					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(4);
				}

				ImGui::SetCursorPos(ImVec2(0.f, 0.f));
				char invId[48];
				std::snprintf(invId, sizeof(invId), "##charinv%zu", i);
				const float invW = std::max(0.f, ImGui::GetWindowWidth() - 8.f - kDeleteBtnW - 16.f);
				ImGui::InvisibleButton(invId, ImVec2(invW, kRowH));
				if (ImGui::IsItemClicked() && m_authPresenter != nullptr)
				{
					// Cliquer ailleurs annule la confirmation de suppression en cours.
					if (pendingDeleteIdx >= 0)
						m_authPresenter->ImGuiCancelDeleteCharacterConfirm();
					m_authPresenter->ImGuiSelectCharacterEntry(static_cast<int>(i));
				}

				ImGui::EndChild();
				ImGui::Spacing();
			}
		}

		ImGui::EndChild();
		ImGui::Spacing();

		// 3 boutons : Retour | Creer nouveau | Jouer
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
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.15f)));
			ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
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
		ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.25f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.35f)));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
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
		ImGui::EndChild();
	}
} // namespace engine::render

#endif
