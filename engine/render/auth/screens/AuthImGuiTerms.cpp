// AUTH-UI.10 — rendu ImGui écran CGU (split depuis AuthImGuiRenderer.cpp).

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
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}
	} // namespace

	void AuthImGuiRenderer::RenderTermsScreen(const RenderModel& rm, float vpW, float vpH)
	{
		(void)vpW;
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

		const std::string title = rm.sectionTitle.empty() ? tr("auth.panel.terms", "Terms of use") : rm.sectionTitle;
		if (!BeginPanel(560.f, vpW, vpH, title.c_str(), "", ""))
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
		ImGui::BeginChild("##terms_scroll", ImVec2(-FLT_MIN, 260.f), true, ImGuiWindowFlags_None);
		if (m_authPresenter != nullptr)
		{
			const std::string& full = m_authPresenter->TermsFullTextForImGui();
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::TextUnformatted(full.c_str());
			ImGui::PopStyleColor();
			const float scrollY = ImGui::GetScrollY();
			const float scrollMax = ImGui::GetScrollMaxY();
			const bool atBottom = (scrollMax <= 1.f) || (scrollMax > 0.f && scrollY >= scrollMax - 4.f);
			m_authPresenter->ImGuiNotifyTermsScrollReachedBottom(atBottom);
		}
		else
		{
			ImGui::TextDisabled("(Texte indisponible.)");
		}
		ImGui::EndChild();

		bool termsAckChecked = false;
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
		if (DrawGhostButton("Refuser") && m_authPresenter != nullptr && m_authWindow != nullptr)
		{
			m_authPresenter->ImGuiTermsDecline(*m_authWindow);
		}
		ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
		if (DrawPrimaryButton("Accepter / continuer") && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiTermsPrimaryClick(*m_authCfg);
		}
		EndPanel();
	}
} // namespace engine::render

#endif
