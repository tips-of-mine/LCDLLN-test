// AUTH-UI.9 - rendu ImGui ecran mot de passe oublie (split depuis AuthImGuiRenderer.cpp).

#include "src/client/render/AuthImGuiRenderer.h"

#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	/// Affiche le panneau de recuperation de mot de passe : champ e-mail, bouton Envoyer et bouton Retour.
	void AuthImGuiRenderer::RenderForgotScreen(const RenderModel& rm, float vpW, float vpH)
	{
		/// Resout une cle de traduction via le presentateur ; retombe sur le fallback si absent.
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

		const std::string title = rm.sectionTitle.empty() ? tr("auth.section.forgot_password", "Password recovery") : rm.sectionTitle;
		if (!BeginPanel(460.f, vpW, vpH, title.c_str(), "", ""))
		{
			EndPanel();
			return;
		}
		const std::string emailLabel = rm.fields.empty() ? tr("common.email", "Email") : rm.fields[0].label;
		DrawField(emailLabel.c_str(), m_forgotEmail, static_cast<int>(sizeof(m_forgotEmail)));

		std::string submitLabel = tr("common.submit", "Submit");
		std::string backLabel = tr("common.back", "Back");
		for (const auto& a : rm.actions)
		{
			if (a.primary)
			{
				submitLabel = a.label;
			}
			else
			{
				backLabel = a.label;
			}
		}

		if (DrawPrimaryButton(submitLabel.c_str()) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitForgotPassword(*m_authCfg, m_forgotEmail);
		}
		ImGui::SameLine(0.f, 8.f);
		if (DrawGhostButton(backLabel.c_str()) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromForgotToLogin();
		}
		EndPanel();
	}
} // namespace engine::render

#endif
