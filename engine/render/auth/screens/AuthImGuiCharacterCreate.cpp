// AUTH-UI.11 — rendu ImGui écran de création de personnage avec saisie du nom et confirmation (split depuis AuthImGuiRenderer.cpp).
// Contient RenderCharCreateScreen : panneau avec champ de nom, lignes d'information issues du modèle, et boutons Annuler / Créer.

#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/LnTheme.h"

#include <string>

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
	} // namespace

	/// Affiche l'écran de création de personnage : champ de saisie du nom, lignes d'information issues du modèle, puis boutons Annuler et Créer.
	void AuthImGuiRenderer::RenderCharCreateScreen(const RenderModel& rm, float vpW, float vpH)
	{
		// Cohérence avec les autres écrans (Login / Register / VerifyEmail / Error) :
		// le grand titre « LES CHRONIQUES » et son sous-titre « de la Lune Noire » sont
		// dessinés AU-DESSUS du cadre, jamais comme titre de panneau. Le panneau ne
		// porte que la section (« Création du personnage » via rm.sectionTitle).
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

		const std::string panelTitle = rm.sectionTitle.empty() ? std::string("Creation de personnage") : rm.sectionTitle;
		if (!BeginPanel(680.f, vpW, vpH, panelTitle.c_str(), "", ""))
		{
			EndPanel();
			return;
		}
		const std::string& nameLabel = rm.fields.empty() ? std::string("Nom du personnage") : rm.fields[0].label;
		DrawField(nameLabel.c_str(), m_charName, static_cast<int>(sizeof(m_charName)));
		for (const auto& line : rm.bodyLines)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", line.text.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
		// Largeurs finies pour éviter que Annuler (pleine largeur) ne capture les clics destinés
		// à Créer (cf. correctif AuthImGuiTerms.cpp pour la même classe de bug).
		const float ccGap = 8.f;
		const float ccBtnW = (ImGui::GetContentRegionAvail().x - ccGap) * 0.5f;
		if (DrawGhostButton("Annuler", false, ccBtnW) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCancelCharacterCreateReturnToLogin();
		}
		ImGui::SameLine(0.f, ccGap);
		std::string submitLabel = "Creer"; ///< Libellé du bouton de confirmation, surchargé par l'action primaire du modèle si présente.
		for (const auto& a : rm.actions)
		{
			if (a.primary)
			{
				submitLabel = a.label;
				break;
			}
		}
		if (DrawPrimaryButton(submitLabel.c_str(), false, ccBtnW) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitCharacterCreate(*m_authCfg, m_charName);
		}
		EndPanel();
	}
} // namespace engine::render

#endif
