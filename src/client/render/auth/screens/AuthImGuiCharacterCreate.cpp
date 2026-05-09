// AUTH-UI.11 - rendu ImGui ecran de creation de personnage avec saisie du nom et confirmation (split depuis AuthImGuiRenderer.cpp).
// Contient RenderCharCreateScreen : panneau avec champ de nom, lignes d'information issues du modele, et boutons Annuler / Creer.

#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/LnTheme.h"

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

	/// Affiche l'ecran de creation de personnage : champ de saisie du nom, lignes d'information issues du modele, puis boutons Annuler et Creer.
	void AuthImGuiRenderer::RenderCharCreateScreen(const RenderModel& rm, float vpW, float vpH)
	{
		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "charcreate");
		const float titleZoneW = vpW * 0.96f;

		const std::string panelTitle = rm.sectionTitle.empty() ? std::string("Creation de personnage") : rm.sectionTitle;
		if (!BeginPanel(680.f, titleZoneW, vpH, panelTitle.c_str(), "", ""))
		{
			EndPanel();
			ImGui::EndChild();
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

		// Choix de la race : combo aligne sur les 6 races jouables seedees par la
		// migration 0036. L'id string envoye au serveur (raceId) doit correspondre
		// a la colonne `races.name` ; les libelles affiches sont en francais natif.
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
		ImGui::TextUnformatted("RACE");
		ImGui::PopStyleColor();
		static constexpr const char* kRaceIds[]    = { "humains", "elfes", "orcs", "nains", "demons", "chevaliers_dragons" };
		static constexpr const char* kRaceLabels[] = { "Humain",  "Elfe",  "Orc",  "Nain",  "Demon",  "Chevalier-dragon"   };
		static constexpr int kRaceCount = sizeof(kRaceIds) / sizeof(kRaceIds[0]);
		if (m_charRaceIdx < 0 || m_charRaceIdx >= kRaceCount) m_charRaceIdx = 0;
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::Combo("##charcreate_race", &m_charRaceIdx, kRaceLabels, kRaceCount);
		ImGui::Spacing();
		// Largeurs finies pour eviter que Annuler (pleine largeur) ne capture les clics destines
		// a Creer (cf. correctif AuthImGuiTerms.cpp pour la meme classe de bug).
		const float ccGap = 8.f;
		const float ccBtnW = (ImGui::GetContentRegionAvail().x - ccGap) * 0.5f;
		if (DrawGhostButton("Annuler", false, ccBtnW) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCancelCharacterCreateReturnToLogin();
		}
		ImGui::SameLine(0.f, ccGap);
		std::string submitLabel = "Creer"; ///< Libelle du bouton de confirmation, surcharge par l'action primaire du modele si presente.
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
			static constexpr const char* kRaceIdsSubmit[] = { "humains", "elfes", "orcs", "nains", "demons", "chevaliers_dragons" };
			static constexpr int kRaceCountSubmit = sizeof(kRaceIdsSubmit) / sizeof(kRaceIdsSubmit[0]);
			const int raceIdx = (m_charRaceIdx >= 0 && m_charRaceIdx < kRaceCountSubmit) ? m_charRaceIdx : 0;
			m_authPresenter->ImGuiSubmitCharacterCreate(*m_authCfg, m_charName, kRaceIdsSubmit[raceIdx]);
		}
		EndPanel();
		ImGui::EndChild();
	}
} // namespace engine::render

#endif
