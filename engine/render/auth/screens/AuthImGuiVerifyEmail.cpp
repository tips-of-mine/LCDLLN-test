#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/auth/AuthImGuiCommon.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cstring>
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

		std::string PackVerifySlotsInOrder(const char slots[7])
		{
			std::string o;
			o.reserve(6u);
			for (int i = 0; i < 6; ++i)
			{
				if (slots[i] >= '0' && slots[i] <= '9')
				{
					o.push_back(slots[i]);
				}
			}
			return o;
		}

		bool VerifySlotsAllSixDigits(const char slots[7])
		{
			for (int i = 0; i < 6; ++i)
			{
				if (slots[i] < '0' || slots[i] > '9')
				{
					return false;
				}
			}
			return true;
		}

		static int g_verifyDigitFocusSlot = -1;
	}

	void AuthImGuiRenderer::RenderVerifyScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const std::string& h1 = rm.titleLine1.empty() ? std::string("LES CHRONIQUES") : rm.titleLine1;
		const std::string& h2 = rm.titleLine2.empty() ? std::string("DE LA LUNE NOIRE") : rm.titleLine2;

		ImGui::SetWindowFontScale(1.62f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(h1.c_str()).x;
		ImGui::SetCursorPos(ImVec2((vpW - w1) * 0.5f, vpH * 0.05f));
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

		DrawRegisterFlowHeader(rm, vpW);

		std::string panelTitle = rm.authVerifyPanelTitle.empty() ? std::string("VERIFIEZ VOTRE COURRIEL") : rm.authVerifyPanelTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string& sub =
			rm.authVerifyPanelSubtitle.empty() ? std::string("NOUS AVONS ENVOYE UN CODE A 6 CHIFFRES.") : rm.authVerifyPanelSubtitle;
		const std::string& ver =
			rm.authVerifyPanelBadge.empty() ? std::string("3 / 4") : rm.authVerifyPanelBadge;
		if (!BeginPanel(560.f, vpW, vpH, panelTitle, sub, ver, true, false))
		{
			EndPanel();
			DrawAuthTweaksPanel(vpW, vpH);
			return;
		}

		if (!rm.errorText.empty())
		{
			DrawAuthBanner("Echec", rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);
		}

		const std::string& digitLab =
			rm.authVerifyDigitLabel.empty() ? std::string("CODE DE VERIFICATION") : rm.authVerifyDigitLabel;
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.78f);
		ImGui::TextUnformatted(digitLab.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const float boxW = (std::min)(56.f, (std::max)(40.f, vpW * 0.06f));
		const float gap = 8.f;
		const float rowW = boxW * 6.f + gap * 5.f;
		ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - rowW) * 0.5f + ImGui::GetCursorPosX());

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 14.f));
		for (int i = 0; i < 6; ++i)
		{
			if (g_verifyDigitFocusSlot == i)
			{
				ImGui::SetKeyboardFocusHere(0);
				g_verifyDigitFocusSlot = -1;
			}
			if (i > 0)
			{
				ImGui::SameLine(0.f, gap);
			}
			ImGui::PushID(i);
			const ImGuiID digitId = ImGui::GetID("##vd");
			const bool borderActive = (ImGui::GetFocusID() == digitId);
			char one[8]{};
			if (m_verifyCode[i] >= '0' && m_verifyCode[i] <= '9')
			{
				one[0] = m_verifyCode[i];
			}
			ImGui::SetNextItemWidth(boxW);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, borderActive ? IV(LnTheme::kPrimary) : IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			ImGui::SetWindowFontScale((std::min)(1.28f, (std::max)(1.f, vpW * 0.0022f)));
			const bool edited = ImGui::InputText("##vd", one, sizeof(one),
				ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_NoHorizontalScroll | ImGuiInputTextFlags_AutoSelectAll);
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
			if (edited)
			{
				const char c = one[0];
				if (c >= '0' && c <= '9')
				{
					m_verifyCode[i] = c;
					if (i < 5)
					{
						g_verifyDigitFocusSlot = i + 1;
					}
				}
				else
				{
					m_verifyCode[i] = '\0';
				}
			}
			ImGui::PopID();
		}
		ImGui::PopStyleVar(1);

		if (m_authPresenter != nullptr)
		{
			const std::string packed = PackVerifySlotsInOrder(m_verifyCode);
			m_authPresenter->ImGuiSetVerifyEmailPartialCode(packed);
		}

		ImGui::Spacing();
		if (!rm.authEmailConfirmationPendingPanel && !rm.authVerifyResendLabel.empty() && !rm.authVerifyChangeEmailLabel.empty())
		{
			const std::string& resend = rm.authVerifyResendLabel;
			const std::string& chmail = rm.authVerifyChangeEmailLabel;
			const float lw = ImGui::CalcTextSize(resend.c_str()).x + 20.f + ImGui::CalcTextSize(chmail.c_str()).x;
			ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - lw) * 0.5f + ImGui::GetCursorPosX());
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.06f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.10f)));
			if (ImGui::SmallButton((resend + "##resend").c_str()) && m_authPresenter != nullptr)
			{
				std::memset(m_verifyCode, 0, sizeof(m_verifyCode));
				m_authPresenter->ImGuiVerifyEmailClearDigits();
			}
			ImGui::SameLine(0.f, 20.f);
			if (ImGui::SmallButton((chmail + "##chgmail").c_str()) && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiVerifyEmailBackToEditRegisterEmail();
			}
			ImGui::PopStyleColor(4);
			ImGui::Spacing();
		}

		const bool codeComplete = VerifySlotsAllSixDigits(m_verifyCode);
		const std::string& backLbl = rm.authVerifyBackLabel.empty() ? std::string("RETOUR") : rm.authVerifyBackLabel;
		const std::string& kcap = rm.authVerifyBackKeycap.empty() ? std::string("ECHAP") : rm.authVerifyBackKeycap;
		const std::string& submitLbl =
			rm.authVerifySubmitLabel.empty() ? std::string("Valider le code") : rm.authVerifySubmitLabel;

		const float capPad = 10.f;
		const float capW = ImGui::CalcTextSize(kcap.c_str()).x + capPad * 2.f;
		ImGui::Columns(2, "##verify_actions", false);
		ImGui::SetColumnWidth(0, 248.f);
		{
			const float colW = ImGui::GetContentRegionAvail().x;
			const float backBtnW = (std::max)(80.f, colW - capW - 10.f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.15f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			if (ImGui::Button(backLbl.c_str(), ImVec2(backBtnW, 36.f)) && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiBackFromVerifyToLogin();
			}
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			ImGui::SameLine(0.f, 8.f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
			ImGui::BeginChild("##verify_keycap", ImVec2(capW, 36.f), true, ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			const float tcx = (capW - ImGui::CalcTextSize(kcap.c_str()).x) * 0.5f;
			ImGui::SetCursorPosX(tcx);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
			ImGui::TextUnformatted(kcap.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();
		}
		ImGui::NextColumn();
		{
			if (DrawPrimaryButton(submitLbl, !codeComplete) && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				const std::string p = PackVerifySlotsInOrder(m_verifyCode);
				m_authPresenter->ImGuiSubmitVerifyEmailCode(*m_authCfg, p.c_str());
			}
			if (!rm.authVerifySubmitKeycap.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				const float sk = ImGui::CalcTextSize(rm.authVerifySubmitKeycap.c_str()).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - sk) * 0.5f);
				ImGui::TextUnformatted(rm.authVerifySubmitKeycap.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
			}
		}
		ImGui::Columns(1);

		EndPanel();

		if (!rm.authVerifyDevHint.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.88f);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.92f);
			const float hintW = (std::min)(560.f, vpW * 0.92f);
			ImGui::SetCursorPosX((vpW - hintW) * 0.5f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + hintW);
			ImGui::TextWrapped("%s", rm.authVerifyDevHint.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleVar(1);
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}

		DrawAuthTweaksPanel(vpW, vpH);

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromVerifyToLogin();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && codeComplete && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			const std::string p = PackVerifySlotsInOrder(m_verifyCode);
			m_authPresenter->ImGuiSubmitVerifyEmailCode(*m_authCfg, p.c_str());
		}
	}

	void AuthImGuiRenderer::RenderEmailConfirmationScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const std::string& h1 = rm.titleLine1.empty() ? std::string("LES CHRONIQUES") : rm.titleLine1;
		const std::string& h2 = rm.titleLine2.empty() ? std::string("DE LA LUNE NOIRE") : rm.titleLine2;

		ImGui::SetWindowFontScale(1.62f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(h1.c_str()).x;
		ImGui::SetCursorPos(ImVec2((vpW - w1) * 0.5f, vpH * 0.05f));
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

		DrawRegisterFlowHeader(rm, vpW);

		std::string panelTitle = rm.authVerifyPanelTitle.empty() ? std::string("INSCRIPTION REUSSIE") : rm.authVerifyPanelTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string& sub = rm.authVerifyPanelSubtitle;
		const std::string& ver =
			rm.authVerifyPanelBadge.empty() ? std::string("3 / 4") : rm.authVerifyPanelBadge;
		if (!BeginPanel(560.f, vpW, vpH, panelTitle, sub, ver, true, false))
		{
			EndPanel();
			DrawAuthTweaksPanel(vpW, vpH);
			return;
		}

		if (!rm.authEmailConfirmationOkTitle.empty() && !rm.authEmailConfirmationOkBody.empty())
		{
			DrawAuthBanner(rm.authEmailConfirmationOkTitle, rm.authEmailConfirmationOkBody, LnTheme::kSuccess.r, LnTheme::kSuccess.g,
				LnTheme::kSuccess.b);
		}

		const std::string& digitLab =
			rm.authVerifyDigitLabel.empty() ? std::string("CODE DE VERIFICATION") : rm.authVerifyDigitLabel;
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.78f);
		ImGui::TextUnformatted(digitLab.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const float boxW = (std::min)(56.f, (std::max)(40.f, vpW * 0.06f));
		const float gap = 8.f;
		const float rowW = boxW * 6.f + gap * 5.f;
		ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - rowW) * 0.5f + ImGui::GetCursorPosX());

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 14.f));
		for (int i = 0; i < 6; ++i)
		{
			if (g_verifyDigitFocusSlot == i)
			{
				ImGui::SetKeyboardFocusHere(0);
				g_verifyDigitFocusSlot = -1;
			}
			if (i > 0)
			{
				ImGui::SameLine(0.f, gap);
			}
			ImGui::PushID(i + 100);
			const ImGuiID digitId = ImGui::GetID("##vd");
			const bool borderActive = (ImGui::GetFocusID() == digitId);
			char one[8]{};
			if (m_verifyCode[i] >= '0' && m_verifyCode[i] <= '9')
			{
				one[0] = m_verifyCode[i];
			}
			ImGui::SetNextItemWidth(boxW);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, borderActive ? IV(LnTheme::kPrimary) : IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			ImGui::SetWindowFontScale((std::min)(1.28f, (std::max)(1.f, vpW * 0.0022f)));
			const bool edited = ImGui::InputText("##vd", one, sizeof(one),
				ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_NoHorizontalScroll | ImGuiInputTextFlags_AutoSelectAll);
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
			if (edited)
			{
				const char c = one[0];
				if (c >= '0' && c <= '9')
				{
					m_verifyCode[i] = c;
					if (i < 5)
					{
						g_verifyDigitFocusSlot = i + 1;
					}
				}
				else
				{
					m_verifyCode[i] = '\0';
				}
			}
			ImGui::PopID();
		}
		ImGui::PopStyleVar(1);

		if (m_authPresenter != nullptr)
		{
			const std::string packed = PackVerifySlotsInOrder(m_verifyCode);
			m_authPresenter->ImGuiSetVerifyEmailPartialCode(packed);
		}

		ImGui::Spacing();

		const bool codeComplete = VerifySlotsAllSixDigits(m_verifyCode);
		const std::string& backLbl =
			rm.authVerifyBackLabel.empty() ? std::string("RETOUR A LA CONNEXION") : rm.authVerifyBackLabel;
		const std::string& kcap = rm.authVerifyBackKeycap.empty() ? std::string("ECHAP") : rm.authVerifyBackKeycap;
		const std::string& submitLbl =
			rm.authVerifySubmitLabel.empty() ? std::string("Valider le code") : rm.authVerifySubmitLabel;

		const float capPad = 10.f;
		const float capW = ImGui::CalcTextSize(kcap.c_str()).x + capPad * 2.f;
		ImGui::Columns(2, "##emailconf_actions", false);
		ImGui::SetColumnWidth(0, 248.f);
		{
			const float colW = ImGui::GetContentRegionAvail().x;
			const float backBtnW = (std::max)(80.f, colW - capW - 10.f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.15f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			if (ImGui::Button(backLbl.c_str(), ImVec2(backBtnW, 36.f)) && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiEmailConfirmationBackToLogin();
			}
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			ImGui::SameLine(0.f, 8.f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
			ImGui::BeginChild("##emailconf_keycap", ImVec2(capW, 36.f), true, ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			const float tcx = (capW - ImGui::CalcTextSize(kcap.c_str()).x) * 0.5f;
			ImGui::SetCursorPosX(tcx);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
			ImGui::TextUnformatted(kcap.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();
		}
		ImGui::NextColumn();
		{
			if (DrawPrimaryButton(submitLbl, !codeComplete) && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				const std::string p = PackVerifySlotsInOrder(m_verifyCode);
				m_authPresenter->ImGuiSubmitVerifyEmailCode(*m_authCfg, p.c_str());
			}
			if (!rm.authVerifySubmitKeycap.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				const float sk = ImGui::CalcTextSize(rm.authVerifySubmitKeycap.c_str()).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - sk) * 0.5f);
				ImGui::TextUnformatted(rm.authVerifySubmitKeycap.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
			}
		}
		ImGui::Columns(1);

		EndPanel();

		if (!rm.authVerifyDevHint.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.88f);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.92f);
			const float hintW = (std::min)(560.f, vpW * 0.92f);
			ImGui::SetCursorPosX((vpW - hintW) * 0.5f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + hintW);
			ImGui::TextWrapped("%s", rm.authVerifyDevHint.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleVar(1);
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}

		DrawAuthTweaksPanel(vpW, vpH);

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiEmailConfirmationBackToLogin();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && codeComplete && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			const std::string p = PackVerifySlotsInOrder(m_verifyCode);
			m_authPresenter->ImGuiSubmitVerifyEmailCode(*m_authCfg, p.c_str());
		}
	}
} // namespace engine::render

#endif
