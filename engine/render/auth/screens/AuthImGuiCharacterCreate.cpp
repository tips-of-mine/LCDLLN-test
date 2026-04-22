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
		const std::string title = rm.titleLine1.empty() ? std::string("Creation de personnage") : rm.titleLine1;
		if (!BeginPanel(680.f, vpW, vpH, title.c_str(), rm.titleLine2.c_str(), ""))
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
		if (DrawGhostButton("Annuler") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCancelCharacterCreateReturnToLogin();
		}
		ImGui::SameLine(0.f, 8.f);
		std::string submitLabel = "Creer"; ///< Libellé du bouton de confirmation, surchargé par l'action primaire du modèle si présente.
		for (const auto& a : rm.actions)
		{
			if (a.primary)
			{
				submitLabel = a.label;
				break;
			}
		}
		if (DrawPrimaryButton(submitLabel.c_str()) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitCharacterCreate(*m_authCfg, m_charName);
		}
		EndPanel();
	}
} // namespace engine::render

#endif
