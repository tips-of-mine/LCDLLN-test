// AUTH-UI.4 — rendu ImGui de l'écran d'erreur d'authentification (erreur simple ou erreur enrichie d'inscription).
#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/auth/AuthImGuiCommon.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Convertit une couleur thème en ImVec4 pour ImGui::PushStyleColor.
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}

		/// Convertit une couleur thème en ImU32 pour les primitives du DrawList (AddRectFilled, etc.).
		ImU32 U32(const LnTheme::Rgba& c)
		{
			return ImGui::ColorConvertFloat4ToU32(IV(c));
		}

		/// Pastilles de variante d’erreur : désactivé en production (maquette AUTH-UI.4).
		constexpr bool kAuthErrorVariantPillsDemo = false;
	}

	/// Affiche l'écran d'erreur : bannière d'erreur, indice de correction et bouton retour ; gère le layout enrichi d'inscription ou le layout simple.
	void AuthImGuiRenderer::RenderErrorScreen(const RenderModel& rm, float vpW, float vpH)
	{
		/// Déclenche l'acquittement de l'erreur et retourne à l'écran précédent.
		auto acknowledge = [this]() {
			if (m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				m_authPresenter->ImGuiAcknowledgeErrorScreen(*m_authCfg);
			}
		};

		const std::string& h1 = rm.titleLine1.empty() ? std::string("Les Chroniques de la Lune Noire") : rm.titleLine1;

		ImGui::SetWindowFontScale(1.62f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(h1.c_str()).x;
		ImGui::SetCursorPos(ImVec2((vpW - w1) * 0.5f, vpH * 0.05f));
		ImGui::TextUnformatted(h1.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();

		if (!rm.titleLine2.empty())
		{
			ImGui::SetWindowFontScale(1.12f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			const float w2 = ImGui::CalcTextSize(rm.titleLine2.c_str()).x;
			ImGui::SetCursorPos(ImVec2((vpW - w2) * 0.5f, ImGui::GetCursorPosY() + 2.f));
			ImGui::TextUnformatted(rm.titleLine2.c_str());
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.f);
		}

		if (rm.authErrorRichRegisterLayout && !rm.authRegisterErrorVariants.empty())
		{
			DrawRegisterFlowHeader(rm, vpW);

			std::string panelTitle = rm.authErrorPanelTitle.empty() ? std::string("ERREUR") : rm.authErrorPanelTitle;
			for (char& ch : panelTitle)
			{
				if (ch >= 'a' && ch <= 'z')
				{
					ch = static_cast<char>(ch - 'a' + 'A');
				}
			}
			const std::string_view sub = rm.authErrorPanelSubtitle;
			const std::string& ver =
				rm.authErrorVersionBadge.empty() ? std::string("Erreur") : rm.authErrorVersionBadge;
			if (!BeginPanel(640.f, vpW, vpH, panelTitle, sub, ver, true, false))
			{
				EndPanel();
				DrawAuthTweaksPanel(vpW, vpH);
				return;
			}

			const int n = static_cast<int>(rm.authRegisterErrorVariants.size());
			const int classified = std::clamp(rm.authRegisterErrorClassifiedIndex, 0, n - 1);
			const int shown = kAuthErrorVariantPillsDemo && m_authErrorPillPreview >= 0
				? std::clamp(m_authErrorPillPreview, 0, n - 1)
				: classified;

			if (kAuthErrorVariantPillsDemo)
			{
				for (int i = 0; i < n; ++i)
				{
					if (i > 0)
					{
						ImGui::SameLine(0.f, 6.f);
					}
					const bool sel = i == shown;
					const auto& pv = rm.authRegisterErrorVariants[static_cast<size_t>(i)];
					ImGui::PushStyleColor(ImGuiCol_Text, sel ? IV(LnTheme::kAccent) : IV(LnTheme::kMuted));
					ImGui::PushStyleColor(ImGuiCol_Border, sel ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, sel ? 1.5f : 1.f);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
					char pid[96]{};
					std::snprintf(pid, sizeof(pid), "%.*s##errpill_%d", static_cast<int>(pv.pillLabel.size()), pv.pillLabel.c_str(), i);
					if (ImGui::SmallButton(pid))
					{
						m_authErrorPillPreview = i;
					}
					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(2);
				}
				ImGui::Spacing();
			}

			const auto& v = rm.authRegisterErrorVariants[static_cast<size_t>(shown)];
			const bool rawBody = rm.authErrorBannerBodyFromUserMessage && shown == classified;
			const std::string& bodyMsg = rawBody ? rm.errorText : v.bannerMessage;
			if (v.warningBanner)
			{
				DrawAuthBanner(v.bannerTitle, bodyMsg, LnTheme::kWarning.r, LnTheme::kWarning.g, LnTheme::kWarning.b);
			}
			else
			{
				DrawAuthBanner(v.bannerTitle, bodyMsg, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);
			}

			if (!v.fieldLabel.empty() && !rm.authErrorHideFieldBox)
			{
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(10.f / 255.f, 13.f / 255.f, 18.f / 255.f, 0.5f));
				ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
				ImGui::BeginChild("##err_field", ImVec2(-FLT_MIN, 0.f), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.78f);
				const std::string& flab =
					rm.authErrorFieldSectionLabel.empty() ? std::string("CHAMP A CORRIGER") : rm.authErrorFieldSectionLabel;
				ImGui::TextUnformatted(flab.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
				ImGui::TextUnformatted(v.fieldLabel.c_str());
				ImGui::PopStyleColor();
				ImGui::EndChild();
				ImGui::Spacing();
			}

			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::AccentDim(0.04f)));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
			ImGui::BeginChild("##err_fix", ImVec2(-FLT_MIN, 0.f), false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 child0 = ImGui::GetWindowPos();
			const ImVec2 pad = ImGui::GetStyle().WindowPadding;
			const float fixH = ImGui::GetFontSize() * 4.f + 24.f;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(child0.x + pad.x, child0.y + pad.y),
				ImVec2(child0.x + pad.x + 3.f, child0.y + pad.y + fixH), U32(LnTheme::kAccent));
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::SetWindowFontScale(0.78f);
			const std::string& hfix =
				rm.authErrorFixSectionLabel.empty() ? std::string("COMMENT CORRIGER") : rm.authErrorFixSectionLabel;
			ImGui::TextUnformatted(hfix.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.95f);
			ImGui::SetWindowFontScale(0.92f);
			ImGui::TextWrapped("%s", v.fixHint.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor();
			ImGui::EndChild();
			ImGui::Spacing();

			const std::string& backLbl =
				rm.authErrorBackButtonLabel.empty() ? std::string("RETOUR AU FORMULAIRE") : rm.authErrorBackButtonLabel;
			const std::string& kcap = rm.authErrorBackKeycap.empty() ? std::string("ECHAP") : rm.authErrorBackKeycap;
			const float capPad = 10.f;
			const float capW = ImGui::CalcTextSize(kcap.c_str()).x + capPad * 2.f;
			const float rowW = ImGui::GetContentRegionAvail().x;
			const float btnW = rowW - capW - 10.f;
			if (ImGui::Button(backLbl.c_str(), ImVec2(btnW < 120.f ? rowW : btnW, 36.f)))
			{
				acknowledge();
			}
			if (btnW >= 120.f)
			{
				ImGui::SameLine(0.f, 10.f);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
				ImGui::BeginChild("##err_keycap", ImVec2(capW, 36.f), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				const float tx = (capW - ImGui::CalcTextSize(kcap.c_str()).x) * 0.5f;
				ImGui::SetCursorPosX(tx);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
				ImGui::TextUnformatted(kcap.c_str());
				ImGui::PopStyleColor();
				ImGui::EndChild();
			}

			if (rm.authErrorShowRetryButton)
			{
				ImGui::Spacing();
				const std::string& retry =
					rm.authErrorRetryButtonLabel.empty() ? std::string("Retry") : rm.authErrorRetryButtonLabel;
				if (DrawPrimaryButton(retry) && m_authPresenter != nullptr && m_authCfg != nullptr)
				{
					m_authPresenter->ImGuiAcknowledgeErrorScreen(*m_authCfg);
				}
			}

			EndPanel();
			DrawAuthTweaksPanel(vpW, vpH);

			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			{
				acknowledge();
			}
			return;
		}

		std::string panelTitle = rm.authErrorPanelTitle.empty() ? std::string("ERREUR") : rm.authErrorPanelTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string& ver = rm.authErrorVersionBadge.empty() ? std::string("Erreur") : rm.authErrorVersionBadge;
		if (!BeginPanel(560.f, vpW, vpH, panelTitle, "", ver, true, false))
		{
			EndPanel();
			DrawAuthTweaksPanel(vpW, vpH);
			return;
		}

		const std::string& banTitle = rm.sectionTitle.empty() ? std::string("Erreur") : rm.sectionTitle;
		DrawAuthBanner(banTitle, rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);

		const std::string& backLbl2 =
			rm.authErrorBackButtonLabel.empty() ? std::string("RETOUR AU FORMULAIRE") : rm.authErrorBackButtonLabel;
		if (DrawGhostButton(backLbl2.c_str()) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiAcknowledgeErrorScreen(*m_authCfg);
		}
		EndPanel();
		DrawAuthTweaksPanel(vpW, vpH);

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			acknowledge();
		}
	}
} // namespace engine::render

#endif
