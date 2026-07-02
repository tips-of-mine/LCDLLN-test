// AUTH-UI.7 - rendu ImGui ecran choix de langue au premier lancement (split depuis AuthImGuiRenderer.cpp).
// Contient DrawLanguageFirstRunCards (grille de cartes drapeaux cliquables) et RenderLangScreen (panneau centre avec titre, cartes et bouton Continuer).

#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/auth/AuthImGuiCommon.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnWidgets.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;
		using LnTheme::ToU32;

		/// Retourne vrai si le tag de locale correspond a une variante anglaise (en, en-GB, en_US...).
		bool IsEnglishLocaleTag(std::string_view tag)
		{
			return tag == "en" || tag == "en-GB" || tag == "en_US" || (tag.size() >= 2 && tag[0] == 'e' && tag[1] == 'n');
		}

		/// Dessine le drapeau approprie dans la carte de langue selon le tag de locale ; repli sur un rectangle neutre pour les locales inconnues.
		void DrawCardFlag(ImDrawList* dl, float x, float y, float fw, float fh, std::string_view tag)
		{
			if (tag == "fr")
			{
				DrawFlagFR(dl, x, y, fw, fh);
			}
			else if (IsEnglishLocaleTag(tag))
			{
				DrawFlagEN(dl, x, y, fw, fh);
			}
			else
			{
				dl->AddRectFilled(ImVec2(x, y), ImVec2(x + fw, y + fh), ToU32(LnTheme::kSurface));
				dl->AddRect(ImVec2(x, y), ImVec2(x + fw, y + fh), ToU32(LnTheme::kBorder), 2.f, 0, 1.f);
			}
		}
	} // namespace

	/// Affiche la grille de cartes de selection de langue (drapeau + nom) et retourne l'index de la carte cliquee, ou -1 si aucun clic.
	int AuthImGuiRenderer::DrawLanguageFirstRunCards(const RenderModel& rm, int selected)
	{
		int clicked = -1;
		const size_t n = rm.languageFirstRunCards.empty() ? 2u : rm.languageFirstRunCards.size();
		const float gap = 16.f; ///< Espacement horizontal entre les cartes.
		// Cartes plus compactes : largeur fixe maxi pour ne pas s'etaler ; centrage horizontal gere ci-dessous.
		const float cardWMax = 200.f;
		const float cardH = 96.f; ///< Hauteur fixe d'une carte (compacte, drapeau centre + libelles).
		const ImVec2 cardSize(cardWMax, cardH);
		const float totalW = cardSize.x * static_cast<float>(n) + gap * static_cast<float>(n > 1u ? n - 1u : 0u);
		const float startX = (ImGui::GetContentRegionAvail().x - totalW) * 0.5f + ImGui::GetCursorPosX(); ///< Position X de depart pour centrer horizontalement la rangee de cartes.

		for (size_t i = 0; i < n; ++i)
		{
			if (i == 0u)
			{
				ImGui::SetCursorPosX(startX);
			}
			else
			{
				ImGui::SameLine(0.f, gap);
			}

			std::string_view locTag = (i == 0u) ? "fr" : "en";
			std::string_view nameCaps = (i == 0u) ? "FRANCAIS" : "ENGLISH";
			std::string_view nativeLn = (i == 0u) ? "Francais" : "English";
			if (!rm.languageFirstRunCards.empty() && i < rm.languageFirstRunCards.size())
			{
				const auto& c = rm.languageFirstRunCards[i];
				locTag = c.localeTag;
				if (!c.nameAllCaps.empty())
				{
					nameCaps = c.nameAllCaps;
				}
				if (!c.nativeLine.empty())
				{
					nativeLn = c.nativeLine;
				}
			}

			const bool isSelected = (static_cast<int>(i) == selected);

			char btnId[48];
			std::snprintf(btnId, sizeof(btnId), "##lang_card_%zu", i);
			ImGui::InvisibleButton(btnId, cardSize);
			if (ImGui::IsItemClicked())
			{
				clicked = static_cast<int>(i);
			}
			const bool itemHovered = ImGui::IsItemHovered();
			const ImVec2 rmin = ImGui::GetItemRectMin();
			const ImVec2 rmax = ImGui::GetItemRectMax();
			ImDrawList* dl = ImGui::GetWindowDrawList();

			if (isSelected)
			{
				dl->AddRectFilled(rmin, rmax, IM_COL32(232, 197, 110, 20), 8.f);
			}

			const ImU32 borderCol =
				isSelected ? ToU32(LnTheme::kAccent) : (itemHovered ? ToU32(LnTheme::kPrimary) : ToU32(LnTheme::kBorder));
			const float borderThick = isSelected ? 2.f : 1.f;
			dl->AddRect(rmin, rmax, borderCol, 8.f, 0, borderThick);

			// Drapeau centre dans la carte (a gauche du libelle) - taille reduite pour cartes compactes.
			const float flagW = 44.f;
			const float flagH = 30.f;
			const float flagPadL = 14.f;
			const float fx = rmin.x + flagPadL;
			const float fy = rmin.y + (cardSize.y - flagH) * 0.5f;
			DrawCardFlag(dl, fx, fy, flagW, flagH, locTag);

			ImFont* font = ImGui::GetFont();
			const float nameSize = font->FontSize * 1.05f;
			const float nativeSz = font->FontSize * 0.88f;
			const float textBlockH = nameSize + 4.f + nativeSz;
			const float textX = fx + flagW + 12.f;
			const float textY = rmin.y + (cardSize.y - textBlockH) * 0.5f;
			const ImVec2 namePos(textX, textY);
			dl->AddText(font, nameSize, namePos, ToU32(LnTheme::kText), nameCaps.data(), nameCaps.data() + static_cast<int>(nameCaps.size()));

			const ImVec2 natPos(textX, textY + nameSize + 4.f);
			dl->AddText(font, nativeSz, natPos, ToU32(LnTheme::kMuted), nativeLn.data(), nativeLn.data() + static_cast<int>(nativeLn.size()));
		}
		return clicked;
	}

	/// Affiche l'ecran complet de selection de langue : titre du jeu, panneau centre avec les cartes de langue et le bouton Continuer, puis hints de navigation en pied de panel.
	void AuthImGuiRenderer::RenderLangScreen(const RenderModel& rm, float vpW, float vpH)
	{
		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "lang");
		const float titleZoneW = vpW * 0.96f;

		std::string panelTitle = rm.sectionTitle.empty() ? std::string("CHOISISSEZ VOTRE LANGUE") : rm.sectionTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string& welcome =
			rm.languagePanelSubtitle.empty() ? std::string("Bienvenue, voyageur.") : rm.languagePanelSubtitle;
		const std::string ver = rm.languageVersionLabel.empty() ? std::string("1 / 2") : rm.languageVersionLabel;
		// Panneau compact : hauteur calee sur titre + sous-titre + cartes (96px) + bouton + footer + paddings.
		// On passe titleZoneW (et non vpW) en 2e arg : le panel se centre desormais dans la stage
		// (BeginChild ##ln_lang_stage), meme logique que l'ecran Login.
		const float panelFixedH = 340.f;
		if (!LnWidgets::BeginPanel(720.f, titleZoneW, vpH, panelTitle, welcome, ver, true, true, panelFixedH))
		{
			LnWidgets::EndPanel();
			ImGui::EndChild();
			return;
		}

		if (m_authPresenter != nullptr)
		{
			const std::string info = m_authPresenter->UiTranslate("language.first_run.info_popup");
			if (!info.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 680.f);
				ImGui::TextWrapped("%s", info.c_str());
				ImGui::PopTextWrapPos();
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
			}
		}
		ImGui::Spacing();

		const int clicked = DrawLanguageFirstRunCards(rm, m_selectedLang);
		if (clicked >= 0)
		{
			m_selectedLang = clicked;
			if (m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiSelectFirstRunLanguageCard(static_cast<uint32_t>(clicked));
			}
		}
		if (!rm.languageFirstRunCards.empty())
		{
			m_selectedLang =
				(std::min)(static_cast<int>(rm.languageFirstRunCards.size()) - 1, (std::max)(0, m_selectedLang));
		}
		ImGui::Spacing();

		std::string contLabel = "Continuer";
		for (const auto& a : rm.actions)
		{
			if (a.primary && a.active && !a.label.empty())
			{
				contLabel = a.label;
				break;
			}
		}
		// Note : pas de suffixe " > " - le glyph n'est pas rendu par la fonte (il apparaissait
		// comme un " ? "). Le libelle reste sobre, suffisant comme call-to-action.

		const float btnW = 200.f;
		ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btnW + ImGui::GetCursorPosX());
		ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.39f, 0.58f, 0.82f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.38f, 0.62f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		char contId[256];
		std::snprintf(contId, sizeof(contId), "%s##lang_continue", contLabel.c_str());
		if (ImGui::Button(contId, ImVec2(btnW, 34.f)) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			std::string_view tag = "fr";
			if (m_selectedLang >= 0 && static_cast<size_t>(m_selectedLang) < rm.languageFirstRunCards.size())
			{
				tag = rm.languageFirstRunCards[static_cast<size_t>(m_selectedLang)].localeTag;
			}
			m_authPresenter->ImGuiApplyFirstRunLanguageContinue(*m_authCfg, tag);
		}
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);

		LnWidgets::Separator();
		const std::string& footL =
			rm.languageFooterLeft.empty() ? std::string("Fleches : naviguer") : rm.languageFooterLeft;
		const std::string& footR = rm.languageFooterRight.empty() ? std::string("Entree : valider") : rm.languageFooterRight;
		LnWidgets::FooterHints(footL, footR);

		LnWidgets::EndPanel();
		ImGui::EndChild();

		DrawAuthTweaksPanel(vpW, vpH);
	}
} // namespace engine::render

#endif
