// AUTH-UI.10 - rendu ImGui ecran d'acceptation des Conditions Generales d'Utilisation (split depuis AuthImGuiRenderer.cpp).
// Contient RenderTermsScreen : panneau avec texte defilant des CGU, case a cocher d'accuse de lecture, et boutons Refuser / Accepter.

#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
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

	/// Affiche l'ecran des CGU : metadonnees en en-tete, texte integral defilant, case a cocher d'acquittement, et boutons Refuser / Accepter.
	void AuthImGuiRenderer::RenderTermsScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const auto tr = [this](const char* key, const char* fallback) -> std::string {
			if (m_authPresenter != nullptr)
			{
				std::string s = m_authPresenter->UiTranslate(key);
				if (!s.empty())
				{
					return s;
				}
			}
			return std::string(fallback);
		};

		// Cadre CGU : largeur 960 px (cap par 92 % vpW sur petits ecrans), hauteur
		// fixee a 78 % vpH. Le panel est centre verticalement *et* horizontalement
		// dans la viewport (BeginPanel gere les deux quand fixedHeight > 0).
		const std::string title = rm.sectionTitle.empty() ? tr("auth.panel.terms", "Terms of use") : rm.sectionTitle;
		const float termsW = (std::min)(960.f, vpW * 0.92f);
		const float termsPanelH = (std::min)(820.f, vpH * 0.78f);
		if (!BeginPanel(termsW, vpW, vpH, title.c_str(), "", "", false, false, termsPanelH))
		{
			EndPanel();
			return;
		}
		{
			const size_t nMeta = std::min<size_t>(rm.bodyLines.size(), 4u);
			for (size_t i = 0; i < nMeta; ++i)
			{
				const auto& line = rm.bodyLines[i];
				if (line.checkbox)
				{
					continue;
				}
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::TextWrapped("%s", line.text.c_str());
				ImGui::PopStyleColor();
			}
		}
		// Reserve 110 px en bas du panel pour la checkbox + separator + 2 boutons.
		// Le scroll prend tout l'espace vertical restant disponible dans le panel
		// (apres meta lines), borne a un minimum 200 px.
		const float termsFooterReserve = 110.f;
		const float termsScrollH = (std::max)(200.f, ImGui::GetContentRegionAvail().y - termsFooterReserve);
		ImGui::BeginChild("##terms_scroll", ImVec2(-FLT_MIN, termsScrollH), true, ImGuiWindowFlags_None);
		if (m_authPresenter != nullptr)
		{
			const std::string& full = m_authPresenter->TermsFullTextForImGui();
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::TextUnformatted(full.c_str());
			ImGui::PopStyleColor();
			const float scrollY = ImGui::GetScrollY();
			const float scrollMax = ImGui::GetScrollMaxY();
			const bool atBottom = (scrollMax <= 1.f) || (scrollMax > 0.f && scrollY >= scrollMax - 4.f); ///< Vrai quand l'utilisateur a atteint le bas du texte (tolerance de 4 px).
			m_authPresenter->ImGuiNotifyTermsScrollReachedBottom(atBottom);
		}
		else
		{
			ImGui::TextDisabled("(Texte indisponible.)");
		}
		ImGui::EndChild();

		bool termsAckChecked = false; ///< Etat courant de la case a cocher d'acquittement, initialise depuis le modele avant le rendu.
		for (const auto& line : rm.bodyLines)
		{
			if (line.checkbox)
			{
				termsAckChecked = line.checkboxChecked;
			}
		}
		if (ImGui::Checkbox("Je reconnais avoir lu et accepte les conditions.", &termsAckChecked))
		{
			if (m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiSetTermsAcknowledgeChecked(termsAckChecked);
			}
		}
		DrawSeparator();
		// Largeurs finies : avant, DrawGhostButton et DrawPrimaryButton utilisaient -FLT_MIN
		// (pleine largeur), donc Refuser couvrait toute la ligne et son rectangle de hit-test
		// englobait celui d'Accepter. ImGui choisit le premier item dessine en cas de chevauchement
		// > cliquer sur " Accepter / continuer " declenchait en fait Refuser > RequestClose() >
		// fermeture immediate du jeu (" le jeu plante ").
		const float btnGap = 14.f;
		const float btnW = (ImGui::GetContentRegionAvail().x - btnGap) * 0.5f;
		if (DrawGhostButton("Refuser", false, btnW) && m_authPresenter != nullptr && m_authWindow != nullptr)
		{
			m_authPresenter->ImGuiTermsDecline(*m_authWindow);
		}
		ImGui::SameLine(0.f, btnGap);
		if (DrawPrimaryButton("Accepter / continuer", false, btnW) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiTermsPrimaryClick(*m_authCfg);
		}
		EndPanel();
	}
} // namespace engine::render

#endif
