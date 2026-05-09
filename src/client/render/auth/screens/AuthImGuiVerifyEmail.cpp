// AUTH-UI.3 - Ecrans de verification d'e-mail (saisie du code 6 chiffres et confirmation d'inscription)

// Contient RenderVerifyScreen (inscription en cours) et RenderEmailConfirmationScreen (inscription reussie, renvoi disponible apres 15 min).
#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/auth/AuthImGuiCommon.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cstring>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Convertit une couleur theme en ImVec4 pour ImGui.
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}

		/// Concatene dans l'ordre les chiffres valides des 6 cases de saisie.
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

		/// Retourne vrai si les 6 cases contiennent chacune un chiffre valide.
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

		static int g_verifyDigitFocusSlot = -1;      ///< Index de la case a focaliser au prochain frame (-1 = aucune).
		static bool g_verifySlotFocused[6]{};         ///< Etat de focus par case pour l'ecran de verification en cours d'inscription.
		static bool g_confSlotFocused[6]{};           ///< Etat de focus par case pour l'ecran de confirmation post-inscription.
	}

	/// Affiche l'ecran de saisie du code a 6 chiffres envoye par e-mail lors de l'inscription.
	void AuthImGuiRenderer::RenderVerifyScreen(const RenderModel& rm, float vpW, float vpH)
	{
		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "verify");
		const float titleZoneW = vpW * 0.96f;

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
		if (!BeginPanel(560.f, titleZoneW, vpH, panelTitle, sub, ver, true, false))
		{
			EndPanel();
			ImGui::EndChild();
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
			const bool borderActive = g_verifySlotFocused[i];
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
			g_verifySlotFocused[i] = ImGui::IsItemFocused();
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
		ImGui::EndChild();

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

	/// Affiche l'ecran " verifiez vos e-mails " affiche apres une inscription reussie, avec renvoi du code si 15 min se sont ecoulees.
	void AuthImGuiRenderer::RenderEmailConfirmationScreen(const RenderModel& rm, float vpW, float vpH)
	{
		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "emailconf");
		const float titleZoneW = vpW * 0.96f;

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
		if (!BeginPanel(560.f, titleZoneW, vpH, panelTitle, sub, ver, true, false))
		{
			EndPanel();
			ImGui::EndChild();
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
			const bool borderActive = g_confSlotFocused[i];
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
			g_confSlotFocused[i] = ImGui::IsItemFocused();
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

		if (rm.authVerifyCanResend)
		{
			if (!rm.authVerifyCodeExpiredMessage.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kErrorCol));
				ImGui::SetWindowFontScale(0.88f);
				const float msgW = ImGui::CalcTextSize(rm.authVerifyCodeExpiredMessage.c_str()).x;
				ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - msgW) * 0.5f + ImGui::GetCursorPosX());
				ImGui::TextUnformatted(rm.authVerifyCodeExpiredMessage.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}
			const std::string resendLbl = rm.authVerifyResendLabel.empty() ? std::string("Renvoyer le code") : rm.authVerifyResendLabel;
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.06f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.10f)));
			const float resendW = ImGui::CalcTextSize(resendLbl.c_str()).x + 16.f;
			ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - resendW) * 0.5f + ImGui::GetCursorPosX());
			if (ImGui::SmallButton((resendLbl + "##resend_conf").c_str()) && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				std::memset(m_verifyCode, 0, sizeof(m_verifyCode));
				m_authPresenter->ImGuiResendVerificationEmail(*m_authCfg);
			}
			ImGui::PopStyleColor(4);
			ImGui::Spacing();
		}

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
		ImGui::EndChild();

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
