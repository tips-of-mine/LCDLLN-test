// AUTH-UI.7 — rendu ImGui écran choix de langue au premier lancement (split depuis AuthImGuiRenderer.cpp).
// Contient DrawLanguageFirstRunCards (grille de cartes drapeaux cliquables) et RenderLangScreen (panneau centré avec titre, cartes et bouton Continuer).

#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/auth/AuthImGuiCommon.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Convertit une couleur LnTheme::Rgba en ImVec4 pour les appels de style ImGui.
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}

		/// Convertit une couleur LnTheme::Rgba en ImU32 pour les appels de draw list ImGui.
		ImU32 U32(const LnTheme::Rgba& c)
		{
			return ImGui::ColorConvertFloat4ToU32(IV(c));
		}

		/// Retourne vrai si le tag de locale correspond à une variante anglaise (en, en-GB, en_US…).
		bool IsEnglishLocaleTag(std::string_view tag)
		{
			return tag == "en" || tag == "en-GB" || tag == "en_US" || (tag.size() >= 2 && tag[0] == 'e' && tag[1] == 'n');
		}

		/// Dessine le drapeau approprié dans la carte de langue selon le tag de locale ; repli sur un rectangle neutre pour les locales inconnues.
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
				dl->AddRectFilled(ImVec2(x, y), ImVec2(x + fw, y + fh), U32(LnTheme::kSurface));
				dl->AddRect(ImVec2(x, y), ImVec2(x + fw, y + fh), U32(LnTheme::kBorder), 2.f, 0, 1.f);
			}
		}
	} // namespace

	/// Affiche la grille de cartes de sélection de langue (drapeau + nom) et retourne l'index de la carte cliquée, ou -1 si aucun clic.
	int AuthImGuiRenderer::DrawLanguageFirstRunCards(const RenderModel& rm, int selected)
	{
		int clicked = -1;
		const size_t n = rm.languageFirstRunCards.empty() ? 2u : rm.languageFirstRunCards.size();
		const float gap = 16.f; ///< Espacement horizontal entre les cartes.
		// Cartes plus compactes : largeur fixe maxi pour ne pas s'étaler ; centrage horizontal géré ci-dessous.
		const float cardWMax = 200.f;
		const float cardH = 96.f; ///< Hauteur fixe d'une carte (compacte, drapeau centré + libellés).
		const ImVec2 cardSize(cardWMax, cardH);
		const float totalW = cardSize.x * static_cast<float>(n) + gap * static_cast<float>(n > 1u ? n - 1u : 0u);
		const float startX = (ImGui::GetContentRegionAvail().x - totalW) * 0.5f + ImGui::GetCursorPosX(); ///< Position X de départ pour centrer horizontalement la rangée de cartes.

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
			std::string_view nativeLn = (i == 0u) ? "Français" : "English";
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
				isSelected ? U32(LnTheme::kAccent) : (itemHovered ? U32(LnTheme::kPrimary) : U32(LnTheme::kBorder));
			const float borderThick = isSelected ? 2.f : 1.f;
			dl->AddRect(rmin, rmax, borderCol, 8.f, 0, borderThick);

			// Drapeau centré dans la carte (à gauche du libellé) — taille réduite pour cartes compactes.
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
			dl->AddText(font, nameSize, namePos, U32(LnTheme::kText), nameCaps.data(), nameCaps.data() + static_cast<int>(nameCaps.size()));

			const ImVec2 natPos(textX, textY + nameSize + 4.f);
			dl->AddText(font, nativeSz, natPos, U32(LnTheme::kMuted), nativeLn.data(), nativeLn.data() + static_cast<int>(nativeLn.size()));
		}
		return clicked;
	}

	/// Affiche l'écran complet de sélection de langue : titre du jeu, panneau centré avec les cartes de langue et le bouton Continuer, puis hints de navigation en pied de panel.
	void AuthImGuiRenderer::RenderLangScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const std::string& h1 = rm.titleLine1.empty() ? std::string("LES CHRONIQUES") : rm.titleLine1;
		const std::string& h2 = rm.titleLine2.empty() ? std::string("DE LA LUNE NOIRE") : rm.titleLine2;

		ImGui::SetWindowFontScale(1.62f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(h1.c_str()).x;
		ImGui::SetCursorPos(ImVec2((vpW - w1) * 0.5f, vpH * 0.07f));
		ImGui::TextUnformatted(h1.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();

		ImGui::SetWindowFontScale(1.12f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
		const float w2 = ImGui::CalcTextSize(h2.c_str()).x;
		ImGui::SetCursorPos(ImVec2((vpW - w2) * 0.5f, ImGui::GetCursorPosY() + 2.f));
		ImGui::TextUnformatted(h2.c_str());
		ImGui::PopStyleColor();
		ImGui::SetWindowFontScale(1.f);

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
		// Panneau compact : hauteur calée sur titre + sous-titre + cartes (96px) + bouton + footer + paddings.
		const float panelFixedH = 320.f;
		if (!BeginPanel(720.f, vpW, vpH, panelTitle, welcome, ver, true, true, panelFixedH))
		{
			EndPanel();
			return;
		}

		if (m_authPresenter != nullptr)
		{
			const std::string info = m_authPresenter->UiTranslate("language.first_run.info_popup");
			if (!info.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
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
		contLabel += "  >";

		const float btnW = 200.f;
		ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btnW + ImGui::GetCursorPosX());
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.39f, 0.58f, 0.82f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.38f, 0.62f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
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

		DrawSeparator();
		const std::string& footL =
			rm.languageFooterLeft.empty() ? std::string("<- -> naviguer") : rm.languageFooterLeft;
		const std::string& footR = rm.languageFooterRight.empty() ? std::string("Entree valider") : rm.languageFooterRight;
		DrawLangFooterHints(footL, footR);

		EndPanel();

		DrawAuthTweaksPanel(vpW, vpH);
	}
} // namespace engine::render

#endif
