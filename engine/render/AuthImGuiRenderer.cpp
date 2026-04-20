#include "engine/render/AuthImGuiRenderer.h"

#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

		ImU32 U32(const LnTheme::Rgba& c)
		{
			return ImGui::ColorConvertFloat4ToU32(IV(c));
		}

		void DrawLocaleFlagMini(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, std::string_view tag)
		{
			const float w = p1.x - p0.x;
			const float h = p1.y - p0.y;
			(void)h;
			if (tag == "fr")
			{
				const float third = w / 3.f;
				dl->AddRectFilled(p0, ImVec2(p0.x + third, p1.y), IM_COL32(0, 85, 164, 255));
				dl->AddRectFilled(ImVec2(p0.x + third, p0.y), ImVec2(p0.x + 2.f * third, p1.y), IM_COL32(255, 255, 255, 255));
				dl->AddRectFilled(ImVec2(p0.x + 2.f * third, p0.y), p1, IM_COL32(239, 65, 53, 255));
			}
			else if (tag == "en" || tag == "en-GB" || tag == "en_US")
			{
				dl->AddRectFilled(p0, p1, IM_COL32(0, 36, 125, 255));
				const float cx = (p0.x + p1.x) * 0.5f;
				const float cy = (p0.y + p1.y) * 0.5f;
				const float t = h * 0.10f;
				const float u = w * 0.10f;
				dl->AddRectFilled(ImVec2(p0.x, cy - t), ImVec2(p1.x, cy + t), IM_COL32(255, 255, 255, 255));
				dl->AddRectFilled(ImVec2(cx - u, p0.y), ImVec2(cx + u, p1.y), IM_COL32(255, 255, 255, 255));
				dl->AddRectFilled(ImVec2(p0.x, cy - t * 0.45f), ImVec2(p1.x, cy + t * 0.45f), IM_COL32(204, 0, 0, 255));
				dl->AddRectFilled(ImVec2(cx - u * 0.45f, p0.y), ImVec2(cx + u * 0.45f, p1.y), IM_COL32(204, 0, 0, 255));
			}
			else
			{
				dl->AddRectFilled(p0, p1, U32(LnTheme::kSurface));
			}
			dl->AddRect(p0, p1, U32(LnTheme::kBorder), 0.f, 0, 1.f);
		}

		void StrCopyTrunc(char* dst, size_t dstSz, const std::string& s)
		{
			if (dst == nullptr || dstSz == 0u)
			{
				return;
			}
			const size_t n = (std::min)(s.size(), dstSz - 1u);
			if (n > 0u)
			{
				std::memcpy(dst, s.data(), n);
			}
			dst[n] = '\0';
		}

		uint32_t VisualFingerprint(const engine::client::AuthUiPresenter::VisualState& v)
		{
			uint32_t x = 0;
			if (v.active)
			{
				x ^= 0x80000000u;
			}
			if (v.languageSelection)
			{
				x ^= 1u << 0;
			}
			if (v.login)
			{
				x ^= 1u << 1;
			}
			if (v.registerMode)
			{
				x ^= 1u << 2;
			}
			if (v.verifyEmail)
			{
				x ^= 1u << 3;
			}
			if (v.error)
			{
				x ^= 1u << 4;
			}
			if (v.options || v.languageOptions)
			{
				x ^= 1u << 5;
			}
			if (v.shardPick)
			{
				x ^= 1u << 6;
			}
			if (v.forgotPassword)
			{
				x ^= 1u << 7;
			}
			if (v.terms)
			{
				x ^= 1u << 8;
			}
			if (v.characterCreate)
			{
				x ^= 1u << 9;
			}
			if (v.submitting)
			{
				x ^= 1u << 10;
			}
			if (v.emailConfirmationPending)
			{
				x ^= 1u << 11;
			}
			return x;
		}
	} // namespace

	void AuthImGuiRenderer::Reset()
	{
		m_selectedLang = 0;
		m_optionsTab = 0;
		m_rememberMe = true;
		m_lastSyncedPhaseToken = 0xffffffffu;
		m_regBirthDayIdx = 0;
		m_regBirthMonthIdx = 0;
		m_regBirthYearIdx = 20;
		m_optDirty = false;
		std::memset(m_loginId, 0, sizeof(m_loginId));
		std::memset(m_loginPw, 0, sizeof(m_loginPw));
		std::memset(m_regId, 0, sizeof(m_regId));
		std::memset(m_regEmail, 0, sizeof(m_regEmail));
		std::memset(m_regFirstName, 0, sizeof(m_regFirstName));
		std::memset(m_regLastName, 0, sizeof(m_regLastName));
		std::memset(m_regCountry, 0, sizeof(m_regCountry));
		std::memset(m_regPw, 0, sizeof(m_regPw));
		std::memset(m_regPw2, 0, sizeof(m_regPw2));
		std::memset(m_verifyCode, 0, sizeof(m_verifyCode));
		std::memset(m_forgotEmail, 0, sizeof(m_forgotEmail));
		std::memset(m_charName, 0, sizeof(m_charName));
		m_langTweakRace = 0;
		m_langTweakAnimBg = true;
	}

	void AuthImGuiRenderer::BindAuthUiBridge(engine::client::AuthUiPresenter* presenter, const engine::core::Config* cfg,
		engine::platform::Window* window)
	{
		m_authPresenter = presenter;
		m_authCfg = cfg;
		m_authWindow = window;
	}

	void AuthImGuiRenderer::PullLanguageOptionsFromPresenter()
	{
		if (m_authPresenter == nullptr)
		{
			return;
		}
		const auto m = m_authPresenter->BuildLanguageOptionsImGuiMirror();
		m_optFullscreen = m.videoFullscreen;
		m_optVsync = m.videoVsync;
		m_optAudioMaster = m.audioMaster01;
		m_optAudioMusic = m.audioMusic01;
		m_optAudioSfx = m.audioSfx01;
		m_optAudioUi = m.audioUi01;
		m_optMouseSens = m.mouseSensitivity;
		m_optInvertY = m.invertY;
		m_optUseZqsd = m.useZqsd;
		m_optGameplayUdp = m.gameplayUdpEnabled;
		m_optAllowInsecureDev = m.allowInsecureDev;
		m_optAuthTimeoutMs = m.authTimeoutMs;
		const auto& locs = m_authPresenter->GetAvailableLocales();
		if (!locs.empty())
		{
			const int maxIdx = static_cast<int>(locs.size()) - 1;
			m_optLangIndex =
				static_cast<int>((m.languageSelectionIndex > static_cast<uint32_t>(maxIdx)) ? static_cast<uint32_t>(maxIdx) : m.languageSelectionIndex);
		}
		else
		{
			m_optLangIndex = 0;
		}
		m_optDirty = false;
	}

	void AuthImGuiRenderer::SyncTransientFromModel(const VisualState& vs, const RenderModel& rm)
	{
		const uint32_t fp = VisualFingerprint(vs);
		if (fp == m_lastSyncedPhaseToken)
		{
			return;
		}
		m_lastSyncedPhaseToken = fp;

		if (vs.login && rm.fields.size() >= 1u)
		{
			StrCopyTrunc(m_loginId, sizeof(m_loginId), rm.fields[0].value);
			m_loginPw[0] = '\0';
		}
		if (vs.registerMode && m_authPresenter != nullptr)
		{
			const auto snap = m_authPresenter->BuildRegisterFieldsMirrorForImGui();
			StrCopyTrunc(m_regId, sizeof(m_regId), snap.login);
			StrCopyTrunc(m_regEmail, sizeof(m_regEmail), snap.email);
			StrCopyTrunc(m_regFirstName, sizeof(m_regFirstName), snap.firstName);
			StrCopyTrunc(m_regLastName, sizeof(m_regLastName), snap.lastName);
			if (snap.countryIso2.size() >= 2u)
			{
				m_regCountry[0] = static_cast<char>(snap.countryIso2[0]);
				m_regCountry[1] = static_cast<char>(snap.countryIso2[1]);
				m_regCountry[2] = '\0';
			}
			else
			{
				StrCopyTrunc(m_regCountry, sizeof(m_regCountry), snap.countryIso2);
			}
			m_regBirthDayIdx = static_cast<int>(std::clamp(snap.birthDayIndex, 0, 30));
			m_regBirthMonthIdx = static_cast<int>(std::clamp(snap.birthMonthIndex, 0, 11));
			m_regBirthYearIdx = static_cast<int>(std::clamp(snap.birthYearIndex, 0, 110));
			m_regPw[0] = '\0';
			m_regPw2[0] = '\0';
		}
		else if (vs.registerMode)
		{
			if (rm.fields.size() > 0u)
			{
				StrCopyTrunc(m_regId, sizeof(m_regId), rm.fields[0].value);
			}
			if (rm.fields.size() > 4u)
			{
				StrCopyTrunc(m_regEmail, sizeof(m_regEmail), rm.fields[4].value);
			}
			m_regPw[0] = '\0';
			m_regPw2[0] = '\0';
		}
		if (vs.verifyEmail && !vs.emailConfirmationPending)
		{
			m_verifyCode[0] = '\0';
		}
		if (vs.forgotPassword && !rm.fields.empty())
		{
			StrCopyTrunc(m_forgotEmail, sizeof(m_forgotEmail), rm.fields[0].value);
		}
		if (vs.languageSelection)
		{
			for (size_t i = 0; i < rm.languageFirstRunCards.size(); ++i)
			{
				if (rm.languageFirstRunCards[i].selected)
				{
					m_selectedLang = static_cast<int>(i);
					break;
				}
			}
		}
		if (vs.options || vs.languageOptions)
		{
			PullLanguageOptionsFromPresenter();
		}
		if (vs.characterCreate)
		{
			if (!rm.fields.empty())
			{
				StrCopyTrunc(m_charName, sizeof(m_charName), rm.fields[0].value);
			}
			else
			{
				m_charName[0] = '\0';
			}
		}
	}

	void AuthImGuiRenderer::Render(const VisualState& vs, const RenderModel& rm, float viewportW, float viewportH)
	{
		if (!vs.active || viewportW <= 0.f || viewportH <= 0.f)
		{
			return;
		}
		SyncTransientFromModel(vs, rm);

		const float overlayAlpha = vs.languageSelection ? 0.22f : 1.f;
		BeginFullscreenOverlay(viewportW, viewportH, overlayAlpha);

		if (vs.languageSelection)
		{
			RenderLangScreen(rm, viewportW, viewportH);
		}
		else if (vs.login)
		{
			RenderLoginScreen(rm, viewportW, viewportH);
		}
		else if (vs.registerMode)
		{
			RenderRegisterScreen(rm, viewportW, viewportH);
		}
		else if (vs.error)
		{
			RenderErrorScreen(rm, viewportW, viewportH);
		}
		else if (vs.verifyEmail)
		{
			if (vs.emailConfirmationPending)
			{
				RenderEmailConfirmationScreen(rm, viewportW, viewportH);
			}
			else
			{
				RenderVerifyScreen(rm, viewportW, viewportH);
			}
		}
		else if (vs.options || vs.languageOptions)
		{
			RenderOptionsScreen(rm, viewportW, viewportH);
		}
		else if (vs.shardPick)
		{
			RenderShardScreen(rm, viewportW, viewportH);
		}
		else if (vs.forgotPassword)
		{
			RenderForgotScreen(rm, viewportW, viewportH);
		}
		else if (vs.terms)
		{
			RenderTermsScreen(rm, viewportW, viewportH);
		}
		else if (vs.characterCreate)
		{
			RenderCharCreateScreen(rm, viewportW, viewportH);
		}
		else if (vs.submitting)
		{
			if (BeginPanel(420.f, viewportW, viewportH, rm.sectionTitle.c_str(), "", ""))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::TextWrapped("%s", rm.infoBanner.c_str());
				ImGui::PopStyleColor();
				EndPanel();
			}
		}

		ImGui::End();
	}

	void AuthImGuiRenderer::BeginFullscreenOverlay(float vpW, float vpH, float windowBgAlpha)
	{
		ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
		ImGui::SetNextWindowSize(ImVec2(vpW, vpH));
		ImGui::SetNextWindowBgAlpha(1.f);
		ImVec4 bg = IV(LnTheme::kBackground);
		bg.w = windowBgAlpha;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::Begin("##ln_auth_overlay",
			nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
				| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(1);
	}

	bool AuthImGuiRenderer::BeginPanel(float width, float vpW, float vpH, std::string_view title,
		std::string_view subtitle, std::string_view versionLabel, bool versionLeadingInfoGlyph, bool subtitleWelcomeAccent)
	{
		const float panelX = (vpW - width) * 0.5f;
		const float panelY = vpH * 0.28f;
		ImGui::SetCursorPos(ImVec2(panelX, panelY));

		ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::PanelBg()));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f, 18.f));

		const bool open = ImGui::BeginChild("##ln_panel", ImVec2(width, 0.f), true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);

		if (!open)
		{
			return false;
		}
		if (!title.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::SetWindowFontScale(1.15f);
			ImGui::TextUnformatted(title.data(), title.data() + static_cast<int>(title.size()));
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}
		if (!versionLabel.empty())
		{
			const float vw = ImGui::CalcTextSize(versionLabel.data(), versionLabel.data() + versionLabel.size()).x;
			const float badge = versionLeadingInfoGlyph ? (ImGui::GetFontSize() + 6.f) : 0.f;
			const float gap = 4.f;
			const float slack = ImGui::GetContentRegionAvail().x - vw - badge - gap;
			ImGui::SameLine(0.f, (slack > 0.f) ? slack : 4.f);
			if (versionLeadingInfoGlyph)
			{
				const ImVec2 ip = ImGui::GetCursorScreenPos();
				const float side = ImGui::GetFontSize() * 0.92f;
				const float r = side * 0.42f;
				const ImVec2 center(ip.x + side * 0.5f, ip.y + side * 0.5f);
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddCircle(center, r, U32(LnTheme::kMuted), 0, 1.25f);
				dl->AddText(ImVec2(center.x - ImGui::CalcTextSize("i").x * 0.5f, ip.y + 1.f), U32(LnTheme::kMuted), "i");
				ImGui::Dummy(ImVec2(side, side));
				ImGui::SameLine(0.f, gap);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextUnformatted(versionLabel.data(), versionLabel.data() + static_cast<int>(versionLabel.size()));
			ImGui::PopStyleColor();
		}
		if (!subtitle.empty())
		{
			if (subtitleWelcomeAccent)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.92f);
				ImGui::SetWindowFontScale(0.95f);
				ImGui::TextWrapped("%.*s", static_cast<int>(subtitle.size()), subtitle.data());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleVar(1);
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::TextWrapped("%.*s", static_cast<int>(subtitle.size()), subtitle.data());
				ImGui::PopStyleColor();
			}
		}
		ImGui::PushStyleColor(ImGuiCol_Separator, IV(LnTheme::kBorder));
		ImGui::Separator();
		ImGui::PopStyleColor();
		ImGui::Spacing();
		return true;
	}

	void AuthImGuiRenderer::EndPanel()
	{
		ImGui::EndChild();
	}

	int AuthImGuiRenderer::DrawLanguageFirstRunCards(const RenderModel& rm, int selected)
	{
		int clicked = -1;
		const size_t n = rm.languageFirstRunCards.empty() ? 2u : rm.languageFirstRunCards.size();
		const float spacing = 18.f;
		const float avail = ImGui::GetContentRegionAvail().x;
		const float cardW = (avail - spacing * static_cast<float>(n > 1u ? n - 1u : 0u)) / static_cast<float>((n < 1u) ? 1u : n);
		const ImVec2 cardSize((cardW > 120.f) ? cardW : 120.f, 128.f);
		const float totalW = cardSize.x * static_cast<float>(n) + spacing * static_cast<float>(n > 1u ? n - 1u : 0u);
		const float startX = (ImGui::GetContentRegionAvail().x - totalW) * 0.5f + ImGui::GetCursorPosX();

		for (size_t i = 0; i < n; ++i)
		{
			if (i == 0u)
			{
				ImGui::SetCursorPosX(startX);
			}
			std::string_view locTag = "fr";
			std::string_view nameCaps = (i == 0u) ? "FRANCAIS" : "ENGLISH";
			std::string_view nativeLn = (i == 0u) ? "Francais" : "English";
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
			const ImVec4 borderCol = isSelected ? IV(LnTheme::BorderActive()) : IV(LnTheme::kBorder);

			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 1.5f : 1.f);

			char childId[32];
			std::snprintf(childId, sizeof(childId), "##lcard%zu", i);
			ImGui::BeginChild(childId, cardSize, true);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			const float flagW = 58.f;
			const float flagH = 40.f;
			const ImVec2 flagPos((cardSize.x - flagW) * 0.5f, 12.f);
			ImGui::SetCursorPos(flagPos);
			const ImVec2 wpos = ImGui::GetWindowPos();
			const ImVec2 fp0(wpos.x + flagPos.x, wpos.y + flagPos.y);
			const ImVec2 fp1(fp0.x + flagW, fp0.y + flagH);
			DrawLocaleFlagMini(ImGui::GetWindowDrawList(), fp0, fp1, locTag);

			ImGui::SetCursorPos(ImVec2(8.f, flagPos.y + flagH + 10.f));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::SetWindowFontScale(1.05f);
			ImGui::PushTextWrapPos(wpos.x + cardSize.x - 8.f);
			ImGui::TextUnformatted(nameCaps.data(), nameCaps.data() + static_cast<int>(nameCaps.size()));
			ImGui::PopTextWrapPos();
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();

			ImGui::SetCursorPos(ImVec2(8.f, ImGui::GetCursorPosY() + 2.f));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.9f);
			ImGui::PushTextWrapPos(wpos.x + cardSize.x - 8.f);
			ImGui::TextUnformatted(nativeLn.data(), nativeLn.data() + static_cast<int>(nativeLn.size()));
			ImGui::PopTextWrapPos();
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();

			ImGui::SetCursorPos(ImVec2(0.f, 0.f));
			ImGui::InvisibleButton(childId, cardSize);
			if (ImGui::IsItemClicked())
			{
				clicked = static_cast<int>(i);
			}

			ImGui::EndChild();
			if (i + 1u < n)
			{
				ImGui::SameLine(0.f, spacing);
			}
		}
		return clicked;
	}

	void AuthImGuiRenderer::DrawLangFooterHints(std::string_view left, std::string_view right)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.88f);
		if (!left.empty() && !right.empty())
		{
			ImGui::TextUnformatted(left.data(), left.data() + static_cast<int>(left.size()));
			const float rw = ImGui::CalcTextSize(right.data(), right.data() + right.size()).x;
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rw - ImGui::GetScrollX());
			ImGui::TextUnformatted(right.data(), right.data() + static_cast<int>(right.size()));
		}
		else if (!left.empty())
		{
			ImGui::TextUnformatted(left.data(), left.data() + static_cast<int>(left.size()));
		}
		else if (!right.empty())
		{
			ImGui::TextUnformatted(right.data(), right.data() + static_cast<int>(right.size()));
		}
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
	}

	void AuthImGuiRenderer::DrawLangScreenTweaks(float vpW, float vpH)
	{
		static constexpr const char* kRaceLabels[] = {"DEFAUT", "HUMAINS", "ELFES", "NAINS", "ORCS", "MORTS-V.", "CORROM.",
			"DIVINS", "DEMONS"};
		const float winW = 272.f;
		ImGui::SetNextWindowPos(ImVec2(vpW - winW - 22.f, vpH - 228.f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(winW, 218.f), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 12.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.78f)));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::Begin("##ln_lang_tweaks",
			nullptr,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus);

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::TextUnformatted("Tweaks");
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("Theme de race");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const float btnW = (ImGui::GetContentRegionAvail().x - 8.f) / 3.f;
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.12f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.18f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				if (c > 0)
				{
					ImGui::SameLine(0.f, 4.f);
				}
				const int idx = r * 3 + c;
				const bool sel = (m_langTweakRace == idx);
				ImGui::PushStyleColor(ImGuiCol_Border, sel ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, sel ? 1.5f : 1.f);
				char id[40];
				std::snprintf(id, sizeof(id), "%s##race_%d", kRaceLabels[idx], idx);
				if (ImGui::Button(id, ImVec2(btnW, 0.f)))
				{
					m_langTweakRace = idx;
				}
				ImGui::PopStyleVar(1);
				ImGui::PopStyleColor(1);
			}
		}
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(3);

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("Fond anime");
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.12f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.18f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
		{
			const float half = (ImGui::GetContentRegionAvail().x - 6.f) * 0.5f;
			const bool on = m_langTweakAnimBg;
			ImGui::PushStyleColor(ImGuiCol_Border, on ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, on ? 1.5f : 1.f);
			if (ImGui::Button("ACTIVE##lang_bg_on", ImVec2(half, 0.f)))
			{
				m_langTweakAnimBg = true;
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
			ImGui::SameLine(0.f, 6.f);
			const bool off = !m_langTweakAnimBg;
			ImGui::PushStyleColor(ImGuiCol_Border, off ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, off ? 1.5f : 1.f);
			if (ImGui::Button("DESACTIVE##lang_bg_off", ImVec2(half, 0.f)))
			{
				m_langTweakAnimBg = false;
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
		}
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(3);

		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
	}

	void AuthImGuiRenderer::DrawField(std::string_view label, char* buf, int bufSz, bool password)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted(label.data(), label.data() + static_cast<int>(label.size()));
		ImGui::PopStyleColor();

		ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

		char inputId[48];
		std::snprintf(inputId, sizeof(inputId), "##f_%p", static_cast<void*>(buf));
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
		if (password)
		{
			flags |= ImGuiInputTextFlags_Password;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText(inputId, buf, static_cast<size_t>(bufSz), flags);

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		ImGui::Spacing();
	}

	void AuthImGuiRenderer::DrawBanner(std::string_view title, std::string_view msg, float r, float g, float b)
	{
		ImVec4 bg(r, g, b, 0.12f);
		ImVec4 bd(r, g, b, 1.f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
		ImGui::PushStyleColor(ImGuiCol_Border, bd);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::BeginChild("##banner", ImVec2(-FLT_MIN, 0.f), true, ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(r, g, b, 1.f));
		if (!title.empty())
		{
			ImGui::TextUnformatted(title.data(), title.data() + static_cast<int>(title.size()));
		}
		ImGui::PopStyleColor();
		if (!msg.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::TextWrapped("%.*s", static_cast<int>(msg.size()), msg.data());
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		ImGui::Spacing();
	}

	void AuthImGuiRenderer::DrawKeycapHints(std::initializer_list<std::pair<const char*, const char*>> hints)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		bool first = true;
		for (const auto& kv : hints)
		{
			if (!first)
			{
				ImGui::SameLine(0.f, 14.f);
			}
			ImGui::Text("[%s] %s", kv.first, kv.second);
			first = false;
		}
		ImGui::PopStyleColor();
	}

	bool AuthImGuiRenderer::DrawPrimaryButton(std::string_view label, bool disabled)
	{
		if (disabled)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.39f, 0.58f, 0.82f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.38f, 0.62f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		char id[160];
		std::snprintf(id, sizeof(id), "%.*s##primary", static_cast<int>(label.size()), label.data());
		const bool clicked = ImGui::Button(id, ImVec2(-FLT_MIN, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (disabled)
		{
			ImGui::EndDisabled();
		}
		return clicked;
	}

	bool AuthImGuiRenderer::DrawGhostButton(std::string_view label, bool disabled)
	{
		if (disabled)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.08f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.15f)));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
		char id[160];
		std::snprintf(id, sizeof(id), "%.*s##ghost", static_cast<int>(label.size()), label.data());
		const bool clicked = ImGui::Button(id, ImVec2(-FLT_MIN, 32.f));
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		if (disabled)
		{
			ImGui::EndDisabled();
		}
		return clicked;
	}

	void AuthImGuiRenderer::DrawSeparator()
	{
		ImGui::PushStyleColor(ImGuiCol_Separator, IV(LnTheme::kBorder));
		ImGui::Separator();
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	void AuthImGuiRenderer::DrawBreadcrumb(std::initializer_list<const char*> steps, int current)
	{
		int i = 0;
		for (const char* s : steps)
		{
			const bool done = i < current;
			const bool active = i == current;
			const ImVec4 col = done ? IV(LnTheme::kSuccess) : (active ? IV(LnTheme::kAccent) : IV(LnTheme::kMuted));
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::Text("%02d %s", i + 1, s);
			ImGui::PopStyleColor();
			++i;
			const int total = static_cast<int>(steps.size());
			if (i < total)
			{
				ImGui::SameLine(0.f, 12.f);
				ImGui::TextUnformatted("›");
				ImGui::SameLine(0.f, 12.f);
			}
		}
		ImGui::Spacing();
	}

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
		if (!BeginPanel(720.f, vpW, vpH, panelTitle, welcome, ver, true, true))
		{
			EndPanel();
			return;
		}

		ImGui::Spacing();
		const int clicked = DrawLanguageFirstRunCards(rm, m_selectedLang);
		if (clicked >= 0)
		{
			m_selectedLang = clicked;
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

		DrawLangScreenTweaks(vpW, vpH);
	}

	void AuthImGuiRenderer::RenderLoginScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const std::string& h1 = rm.titleLine1.empty() ? std::string("LES CHRONIQUES") : rm.titleLine1;
		const std::string& h2 = rm.titleLine2.empty() ? std::string("DE LA LUNE NOIRE") : rm.titleLine2;

		ImGui::SetWindowFontScale(1.4f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::SetCursorPos(ImVec2((vpW - ImGui::CalcTextSize(h1.c_str()).x) * 0.5f, vpH * 0.07f));
		ImGui::TextUnformatted(h1.c_str());
		ImGui::SetCursorPos(ImVec2((vpW - ImGui::CalcTextSize(h2.c_str()).x) * 0.5f, ImGui::GetCursorPosY()));
		ImGui::TextUnformatted(h2.c_str());
		ImGui::PopStyleColor();
		ImGui::SetWindowFontScale(1.f);

		if (!BeginPanel(460.f, vpW, vpH, rm.sectionTitle.empty() ? "Connexion" : rm.sectionTitle, "", ""))
		{
			EndPanel();
			return;
		}

		if (!rm.errorText.empty())
		{
			DrawBanner("Echec", rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);
		}
		if (!rm.infoBanner.empty())
		{
			DrawBanner("Information", rm.infoBanner, LnTheme::kPrimary.r, LnTheme::kPrimary.g, LnTheme::kPrimary.b);
		}

		DrawField("Identifiant", m_loginId, static_cast<int>(sizeof(m_loginId)), false);
		DrawField("Mot de passe", m_loginPw, static_cast<int>(sizeof(m_loginPw)), true);

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::Checkbox("##remember", &m_rememberMe);
		ImGui::SameLine();
		ImGui::TextUnformatted("Se souvenir de moi");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		if (ImGui::SmallButton("Mot de passe oublie ?##forgot_link") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiNavigateToForgotFromLogin();
		}
		ImGui::SameLine(0.f, 10.f);
		if (ImGui::SmallButton("Portail web##forgot_portal") && m_authPresenter != nullptr && m_authCfg != nullptr && m_authWindow != nullptr)
		{
			m_authPresenter->ImGuiOpenForgotPasswordPortal(*m_authCfg, *m_authWindow);
		}
		ImGui::PopStyleColor();

		ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim()));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		if (ImGui::Button("Creer un compte##register_link", ImVec2(0.f, 28.f)) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiNavigateToRegisterFromLogin();
		}
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);

		ImGui::SameLine(0.f, 8.f);
		if (DrawPrimaryButton("Se connecter") && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitLogin(*m_authCfg, m_loginId, m_loginPw, m_rememberMe);
		}

		DrawSeparator();
		DrawKeycapHints({{"Tab", "champ suivant"}, {"Entree", "se connecter"}, {"Echap", "quitter"}});

		EndPanel();

		const float linksY = ImGui::GetCursorPosY() + 10.f;
		const float linksX = (vpW - 160.f) * 0.5f;
		ImGui::SetCursorPos(ImVec2(linksX, linksY));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		if (ImGui::SmallButton("Options##opts_link") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiOpenLanguageOptionsMenu();
		}
		ImGui::SameLine(0.f, 20.f);
		if (ImGui::SmallButton("Quitter##quit_link") && m_authPresenter != nullptr && m_authWindow != nullptr)
		{
			m_authPresenter->ImGuiRequestClose(*m_authWindow);
		}
		ImGui::PopStyleColor();
	}

	void AuthImGuiRenderer::RenderRegisterScreen(const RenderModel& rm, float vpW, float vpH)
	{
		DrawBreadcrumb({"Langue", "Compte", "Courriel", "Monde"}, 1);
		const std::string panelTitle = rm.sectionTitle.empty() ? std::string("Creer un compte") : rm.sectionTitle;
		if (!BeginPanel(720.f, vpW, vpH, panelTitle.c_str(), "Forger votre identite dans les terres de la Lune Noire.", "2 / 4"))
		{
			EndPanel();
			return;
		}

		DrawField("Identifiant", m_regId, static_cast<int>(sizeof(m_regId)));
		DrawField("Adresse courriel", m_regEmail, static_cast<int>(sizeof(m_regEmail)));
		DrawField("Prenom", m_regFirstName, static_cast<int>(sizeof(m_regFirstName)));
		DrawField("Nom", m_regLastName, static_cast<int>(sizeof(m_regLastName)));
		DrawField("Pays (code ISO, ex. FR)", m_regCountry, static_cast<int>(sizeof(m_regCountry)));

		const float halfW = (ImGui::GetContentRegionAvail().x - 12.f) * 0.5f;
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("Mot de passe");
		ImGui::PopStyleColor();
		ImGui::SetNextItemWidth(halfW);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::InputText("##pw_reg", m_regPw, sizeof(m_regPw), ImGuiInputTextFlags_Password);
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(1);

		int strength = 0;
		const size_t pwLen = std::strlen(m_regPw);
		if (pwLen >= 8)
		{
			++strength;
		}
		bool hasUpper = false;
		bool hasDigit = false;
		bool hasSym = false;
		for (size_t i = 0; i < pwLen; ++i)
		{
			const unsigned char c = static_cast<unsigned char>(m_regPw[i]);
			if (c >= 'A' && c <= 'Z')
			{
				hasUpper = true;
			}
			if (c >= '0' && c <= '9')
			{
				hasDigit = true;
			}
			if (!std::isalnum(static_cast<unsigned char>(c)))
			{
				hasSym = true;
			}
		}
		if (hasUpper)
		{
			++strength;
		}
		if (hasDigit)
		{
			++strength;
		}
		if (hasSym)
		{
			++strength;
		}

		const float segW = (halfW - 9.f) / 4.f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 segPos(ImGui::GetWindowPos().x + ImGui::GetCursorPosX(), ImGui::GetWindowPos().y + ImGui::GetCursorPosY() + 2.f);
		for (int s = 0; s < 4; ++s)
		{
			const LnTheme::Rgba col = (s < strength) ? (strength <= 1 ? LnTheme::kErrorCol
														  : strength == 2   ? LnTheme::kWarning
																			: LnTheme::kSuccess)
												   : LnTheme::kBorder;
			dl->AddRectFilled(segPos, ImVec2(segPos.x + segW, segPos.y + 5.f), U32(col), 2.f);
			segPos.x += segW + 3.f;
		}
		ImGui::Dummy(ImVec2(halfW, 8.f));

		ImGui::SameLine(0.f, 12.f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("Confirmation");
		ImGui::PopStyleColor();
		ImGui::SetNextItemWidth(halfW);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::InputText("##pw2_reg", m_regPw2, sizeof(m_regPw2), ImGuiInputTextFlags_Password);
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(1);
		ImGui::Spacing();

		static const char* months[] = {"Janv.", "Fevr.", "Mars", "Avr.", "Mai", "Juin", "Juil.", "Aout", "Sept.", "Oct.", "Nov.", "Dec."};
		static char dayItemsBuf[31][4]{};
		static bool dayInited = false;
		if (!dayInited)
		{
			for (int d = 0; d < 31; ++d)
			{
				std::snprintf(dayItemsBuf[d], sizeof(dayItemsBuf[d]), "%02d", d + 1);
			}
			dayInited = true;
		}
		const char* dayPtrs[31];
		for (int d = 0; d < 31; ++d)
		{
			dayPtrs[d] = dayItemsBuf[d];
		}
		static char yrBufs[90][8]{};
		static const char* yrPtrs[90]{};
		static bool yrInit = false;
		if (!yrInit)
		{
			for (int i = 0; i < 90; ++i)
			{
				std::snprintf(yrBufs[i], sizeof(yrBufs[i]), "%d", 2010 - i);
				yrPtrs[i] = yrBufs[i];
			}
			yrInit = true;
		}

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("Date de naissance");
		ImGui::PopStyleColor();
		const float thirdW = (ImGui::GetContentRegionAvail().x - 16.f) / 3.f;
		ImGui::SetNextItemWidth(thirdW);
		ImGui::Combo("##day", &m_regBirthDayIdx, dayPtrs, 31);
		ImGui::SameLine(0.f, 8.f);
		ImGui::SetNextItemWidth(thirdW);
		ImGui::Combo("##month", &m_regBirthMonthIdx, months, 12);
		ImGui::SameLine(0.f, 8.f);
		ImGui::SetNextItemWidth(thirdW);
		ImGui::Combo("##year", &m_regBirthYearIdx, yrPtrs, 90);
		ImGui::Spacing();

		char dayStr[8]{};
		char monStr[8]{};
		char yrStr[8]{};
		std::snprintf(dayStr, sizeof(dayStr), "%02d", m_regBirthDayIdx + 1);
		std::snprintf(monStr, sizeof(monStr), "%02d", m_regBirthMonthIdx + 1);
		std::snprintf(yrStr, sizeof(yrStr), "%d", 2010 - m_regBirthYearIdx);

		const bool fieldsOk = std::strlen(m_regId) > 0 && std::strlen(m_regEmail) > 0 && std::strlen(m_regFirstName) > 0
			&& std::strlen(m_regLastName) > 0 && std::strlen(m_regCountry) >= 2u;
		const bool canSubmit = fieldsOk && (strength >= 3) && (std::strlen(m_regPw) > 0) && (std::strcmp(m_regPw, m_regPw2) == 0);

		if (DrawGhostButton("Retour") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromRegisterToLogin();
		}
		ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
		if (DrawPrimaryButton("Creer le compte", !canSubmit) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			engine::client::AuthUiPresenter::RegisterImGuiSubmit form{};
			form.login = m_regId;
			form.email = m_regEmail;
			form.password = m_regPw;
			form.passwordConfirm = m_regPw2;
			form.firstName = m_regFirstName;
			form.lastName = m_regLastName;
			form.birthDay = dayStr;
			form.birthMonth = monStr;
			form.birthYear = yrStr;
			form.countryIso2 = m_regCountry;
			m_authPresenter->ImGuiSubmitRegister(*m_authCfg, form);
		}

		DrawSeparator();
		DrawKeycapHints({{"Tab", "champ suivant"}, {"Entree", "valider"}, {"Echap", "retour"}});
		EndPanel();
	}

	void AuthImGuiRenderer::RenderErrorScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const std::string panelTitle = rm.sectionTitle.empty() ? std::string("Erreur") : rm.sectionTitle;
		if (!BeginPanel(640.f, vpW, vpH, panelTitle.c_str(), "", "Erreur"))
		{
			EndPanel();
			return;
		}

		const bool isNetwork = !rm.infoBanner.empty();
		const LnTheme::Rgba bannerColor = isNetwork ? LnTheme::kWarning : LnTheme::kErrorCol;
		const std::string t = rm.titleLine1.empty() ? std::string("Erreur") : rm.titleLine1;
		DrawBanner(t, rm.errorText, bannerColor.r, bannerColor.g, bannerColor.b);

		if (!rm.sectionTitle.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
			ImGui::BeginChild("##err_field", ImVec2(-FLT_MIN, 52.f), true, ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextUnformatted("CHAMP A CORRIGER");
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::TextUnformatted(rm.sectionTitle.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();
			ImGui::Spacing();
		}

		if (!rm.infoBanner.empty() || isNetwork)
		{
			const std::string& conseil = rm.infoBanner.empty() ? rm.errorText : rm.infoBanner;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::AccentDim(0.04f)));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
			ImGui::BeginChild("##err_fix", ImVec2(-FLT_MIN, 0.f), false, ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
			const ImVec2 wpos = ImGui::GetWindowPos();
			ImGui::GetWindowDrawList()->AddRectFilled(wpos, ImVec2(wpos.x + 3.f, wpos.y + 60.f), U32(LnTheme::kAccent));
			ImGui::SetCursorPosX(8.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::TextUnformatted("COMMENT CORRIGER");
			ImGui::PopStyleColor();
			ImGui::SetCursorPosX(8.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::TextWrapped("%s", conseil.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();
			ImGui::Spacing();
		}

		if (DrawGhostButton("Retour au formulaire") && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiAcknowledgeErrorScreen(*m_authCfg);
		}
		if (isNetwork)
		{
			ImGui::SameLine(0.f, 8.f);
			if (DrawPrimaryButton("Continuer") && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				m_authPresenter->ImGuiAcknowledgeErrorScreen(*m_authCfg);
			}
		}
		EndPanel();
	}

	void AuthImGuiRenderer::RenderVerifyScreen(const RenderModel& rm, float vpW, float vpH)
	{
		DrawBreadcrumb({"Langue", "Compte", "Courriel", "Monde"}, 2);
		const std::string sub =
			rm.sectionTitle.empty() ? std::string("Nous avons envoye un code a 6 chiffres.") : rm.sectionTitle;
		if (!BeginPanel(560.f, vpW, vpH, "Verifiez votre courriel", sub, "3 / 4"))
		{
			EndPanel();
			return;
		}

		if (!rm.errorText.empty())
		{
			DrawBanner("Code incorrect", rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("CODE DE VERIFICATION (6 chiffres)");
		ImGui::PopStyleColor();
		ImGui::SetNextItemWidth(220.f);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::InputText("##verify_code", m_verifyCode, sizeof(m_verifyCode), ImGuiInputTextFlags_CharsDecimal);
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(1);
		ImGui::Spacing();

		const float linksW = 220.f;
		ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - linksW) * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		(void)ImGui::SmallButton("Renvoyer le code##resend");
		ImGui::SameLine(0.f, 12.f);
		(void)ImGui::SmallButton("Modifier le courriel##changemail");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const bool codeComplete = (std::strlen(m_verifyCode) == 6u);
		if (DrawGhostButton("Retour") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromVerifyToLogin();
		}
		ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
		if (DrawPrimaryButton("Valider le code", !codeComplete) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitVerifyEmailCode(*m_authCfg, m_verifyCode);
		}

		EndPanel();
	}

	void AuthImGuiRenderer::RenderEmailConfirmationScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const std::string title = rm.sectionTitle.empty() ? std::string("Courriel envoye") : rm.sectionTitle;
		const std::string sub = rm.titleLine2.empty() ? std::string("") : rm.titleLine2;
		if (!BeginPanel(560.f, vpW, vpH, title.c_str(), sub.c_str(), "3 / 4"))
		{
			EndPanel();
			return;
		}
		for (const auto& line : rm.bodyLines)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::TextWrapped("%s", line.text.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
		if (DrawPrimaryButton("Retour connexion") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiEmailConfirmationBackToLogin();
		}
		EndPanel();
	}

	void AuthImGuiRenderer::RenderOptionsScreen(const RenderModel& /*rm*/, float vpW, float vpH)
	{
		const float sideW = 220.f;
		const float mainW = vpW - sideW;
		const float height = vpH;

		struct TabEntry
		{
			const char* icon;
			const char* label;
		};
		static constexpr TabEntry tabs[] = {
			{"[G]", "Graphismes"},
			{"[S]", "Son"},
			{"[K]", "Controles"},
			{"[L]", "Langue"},
			{"[U]", "Interface"},
			{"[N]", "Reseau"},
			{"[A]", "Compte"},
		};
		static constexpr int tabCount = 7;

		ImGui::SetCursorPos(ImVec2(0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kPanel));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::BeginChild("##opts_sidebar", ImVec2(sideW, height), true, ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(2);

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::TextUnformatted("OPTIONS");
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		DrawSeparator();

		for (int i = 0; i < tabCount; ++i)
		{
			const bool active = (m_optionsTab == i);
			ImGui::PushStyleColor(ImGuiCol_Button, active ? IV(LnTheme::AccentDim(0.15f)) : ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_Text, active ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
			char btnId[48];
			std::snprintf(btnId, sizeof(btnId), "%s %s##tab%d", tabs[i].icon, tabs[i].label, i);
			if (ImGui::Button(btnId, ImVec2(-FLT_MIN, 32.f)))
			{
				m_optionsTab = i;
			}
			ImGui::PopStyleColor(3);
		}
		ImGui::EndChild();

		ImGui::SetCursorPos(ImVec2(sideW, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kBackground));
		ImGui::BeginChild("##opts_main", ImVec2(mainW, height), false, ImGuiWindowFlags_None);
		ImGui::PopStyleColor(1);

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("CATEGORIE");
		ImGui::PopStyleColor();
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::SetWindowFontScale(1.2f);
		ImGui::TextUnformatted(tabs[m_optionsTab].label);
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		DrawSeparator();

		ImGui::BeginChild("##opts_body", ImVec2(-FLT_MIN, height - 100.f), false);

		const auto markDirty = [this]() { m_optDirty = true; };
		const auto sliderVol01 = [&](const char* label, float* v01) {
			float pct = *v01 * 100.f;
			if (ImGui::SliderFloat(label, &pct, 0.f, 100.f, "%.0f%%"))
			{
				*v01 = pct * 0.01f;
				markDirty();
			}
		};

		if (m_optionsTab == 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextUnformatted("VIDEO");
			ImGui::PopStyleColor();
			if (ImGui::Checkbox("Plein ecran", &m_optFullscreen))
			{
				markDirty();
			}
			if (ImGui::Checkbox("Synchronisation verticale", &m_optVsync))
			{
				markDirty();
			}
		}
		else if (m_optionsTab == 1)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextUnformatted("VOLUMES (0-100 %)");
			ImGui::PopStyleColor();
			sliderVol01("Volume maitre", &m_optAudioMaster);
			sliderVol01("Musique", &m_optAudioMusic);
			sliderVol01("Effets", &m_optAudioSfx);
			sliderVol01("Interface (UI)", &m_optAudioUi);
		}
		else if (m_optionsTab == 2)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextUnformatted("SOURIS / CLAVIER");
			ImGui::PopStyleColor();
			float sensUi = m_optMouseSens * 1000.f;
			if (ImGui::SliderFloat("Sensibilite (x1000)", &sensUi, 1.f, 10.f, "%.2f"))
			{
				m_optMouseSens = sensUi * 0.001f;
				markDirty();
			}
			if (ImGui::Checkbox("Inverser axe Y", &m_optInvertY))
			{
				markDirty();
			}
			if (ImGui::Checkbox("Disposition ZQSD (AZERTY)", &m_optUseZqsd))
			{
				markDirty();
			}
		}
		else if (m_optionsTab == 3)
		{
			if (m_authPresenter != nullptr)
			{
				const auto& locs = m_authPresenter->GetAvailableLocales();
				std::vector<const char*> ptrs;
				ptrs.reserve(locs.size());
				for (const auto& loc : locs)
				{
					ptrs.push_back(loc.c_str());
				}
				if (!ptrs.empty())
				{
					const int n = static_cast<int>(ptrs.size());
					if (m_optLangIndex >= n)
					{
						m_optLangIndex = n - 1;
					}
					if (ImGui::Combo("Langue d'interface", &m_optLangIndex, ptrs.data(), n))
					{
						markDirty();
					}
				}
			}
			else
			{
				ImGui::TextDisabled("Presenter non lie.");
			}
		}
		else if (m_optionsTab == 4)
		{
			ImGui::TextDisabled("Options d'interface avancees : non reliees au presenter pour l'instant.");
		}
		else if (m_optionsTab == 5)
		{
			if (ImGui::Checkbox("Gameplay UDP (experimental)", &m_optGameplayUdp))
			{
				markDirty();
			}
			if (ImGui::Checkbox("Autoriser dev non securise", &m_optAllowInsecureDev))
			{
				markDirty();
			}
			int tmo = static_cast<int>(m_optAuthTimeoutMs);
			if (ImGui::SliderInt("Delai auth (ms)", &tmo, 1000, 15000, "%d ms"))
			{
				m_optAuthTimeoutMs = static_cast<uint32_t>(tmo);
				markDirty();
			}
		}
		else if (m_optionsTab == 6)
		{
			ImGui::TextDisabled("Compte : utilisez les ecrans de connexion / options compte.");
		}

		ImGui::EndChild();

		DrawSeparator();
		if (DrawGhostButton("Retour") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCloseLanguageOptionsWithoutApply();
		}
		ImGui::SameLine(mainW - 220.f);
		if (DrawGhostButton("Annuler", !m_optDirty) && m_authPresenter != nullptr)
		{
			PullLanguageOptionsFromPresenter();
		}
		ImGui::SameLine(0.f, 8.f);
		if (DrawPrimaryButton("Appliquer", !m_optDirty) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			engine::client::AuthUiPresenter::LanguageOptionsImGuiMirror mir{};
			mir.videoFullscreen = m_optFullscreen;
			mir.videoVsync = m_optVsync;
			mir.audioMaster01 = m_optAudioMaster;
			mir.audioMusic01 = m_optAudioMusic;
			mir.audioSfx01 = m_optAudioSfx;
			mir.audioUi01 = m_optAudioUi;
			mir.mouseSensitivity = m_optMouseSens;
			mir.invertY = m_optInvertY;
			mir.useZqsd = m_optUseZqsd;
			mir.gameplayUdpEnabled = m_optGameplayUdp;
			mir.allowInsecureDev = m_optAllowInsecureDev;
			mir.authTimeoutMs = m_optAuthTimeoutMs;
			mir.languageSelectionIndex = static_cast<uint32_t>(m_optLangIndex);
			m_authPresenter->ImGuiApplyLanguageOptionsMenu(*m_authCfg, mir);
			m_optDirty = false;
		}

		ImGui::EndChild();
	}

	void AuthImGuiRenderer::RenderShardScreen(const RenderModel& rm, float vpW, float vpH)
	{
		(void)vpW;
		DrawBreadcrumb({"Compte", "Royaume", "Personnage", "Entree"}, 1);
		if (!BeginPanel(820.f, vpW, vpH, rm.sectionTitle.empty() ? "Choisissez votre royaume" : rm.sectionTitle.c_str(),
				"Chaque monde possede sa population, ses regles et ses evenements.", "3 / 4"))
		{
			EndPanel();
			return;
		}
		if (!rm.bodyLines.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", rm.bodyLines[0].text.c_str());
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		uint32_t choice = 0;
		if (m_authPresenter != nullptr)
		{
			choice = m_authPresenter->ShardPickChoiceShardId();
		}

		int row = 0;
		if (m_authPresenter != nullptr)
		{
			for (const auto& e : m_authPresenter->ShardPickEntries())
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				const bool isSelected = (choice == e.shard_id);
				const float loadFrac = e.max_capacity > 0u
					? static_cast<float>(e.current_load) / static_cast<float>(e.max_capacity)
					: 0.f;
				const bool saturated = loadFrac > 0.85f;
				const ImVec4 rowBorder = isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder);

				ImGui::PushStyleColor(ImGuiCol_ChildBg, isSelected ? IV(LnTheme::AccentDim(0.06f)) : IV(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border, rowBorder);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);
				char rowId[32];
				std::snprintf(rowId, sizeof(rowId), "##shard%u", e.shard_id);
				ImGui::BeginChild(rowId, ImVec2(-FLT_MIN, 72.f), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);

				ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(1.15f);
				ImGui::Text(" #%u", e.shard_id);
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();

				ImGui::SameLine(72.f);
				ImGui::BeginGroup();
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
				ImGui::TextUnformatted(e.endpoint.c_str());
				ImGui::PopStyleColor();
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::Text("Charge %u / %u joueurs", e.current_load, e.max_capacity);
				ImGui::PopStyleColor();
				ImGui::EndGroup();

				ImGui::SameLine(ImGui::GetWindowWidth() - 200.f);
				const LnTheme::Rgba barCol = saturated ? LnTheme::kWarning : LnTheme::kSuccess;
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IV(barCol));
				ImGui::ProgressBar(loadFrac, ImVec2(160.f, 8.f), "");
				ImGui::PopStyleColor();

				ImGui::SetCursorPos(ImVec2(0.f, 0.f));
				char invId[40];
				std::snprintf(invId, sizeof(invId), "##sinv%u", e.shard_id);
				ImGui::InvisibleButton(invId, ImVec2(ImGui::GetWindowWidth(), 72.f));
				if (ImGui::IsItemClicked() && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiSetShardPickChoiceShardId(e.shard_id);
				}

				ImGui::EndChild();
				ImGui::Spacing();
				++row;
			}
		}

		if (row == 0)
		{
			ImGui::TextDisabled("Aucun royaume en ligne pour le moment.");
		}

		const bool canEnter = (m_authPresenter != nullptr && m_authPresenter->ShardPickChoiceShardId() != 0u);
		if (DrawGhostButton("Retour") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromShardPickToLogin();
		}
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.f);
		DrawKeycapHints({{"Clic", "selection"}});
		ImGui::SameLine(0.f, 12.f);
		if (DrawPrimaryButton("Entrer dans le monde", !canEnter) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitShardPick(*m_authCfg);
		}

		EndPanel();
	}

	void AuthImGuiRenderer::RenderForgotScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const std::string title = rm.sectionTitle.empty() ? std::string("Mot de passe oublie") : rm.sectionTitle;
		if (!BeginPanel(460.f, vpW, vpH, title.c_str(), "", ""))
		{
			EndPanel();
			return;
		}
		DrawField("Adresse courriel", m_forgotEmail, static_cast<int>(sizeof(m_forgotEmail)));
		if (DrawPrimaryButton("Envoyer la demande") && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitForgotPassword(*m_authCfg, m_forgotEmail);
		}
		ImGui::SameLine(0.f, 8.f);
		if (DrawGhostButton("Retour") && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromForgotToLogin();
		}
		EndPanel();
	}

	void AuthImGuiRenderer::RenderTermsScreen(const RenderModel& rm, float vpW, float vpH)
	{
		(void)vpW;
		const std::string title = rm.sectionTitle.empty() ? std::string("Conditions d'utilisation") : rm.sectionTitle;
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
			const float maxY = ImGui::GetScrollMaxY();
			const bool atBottom = maxY <= 1.f || ImGui::GetScrollY() >= maxY - 2.f;
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
		std::string submitLabel = "Creer";
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

#else

#	include <cstdint>

namespace engine::render
{
	void AuthImGuiRenderer::Reset() {}

	void AuthImGuiRenderer::BindAuthUiBridge(engine::client::AuthUiPresenter*, const engine::core::Config*, engine::platform::Window*)
	{
	}

	void AuthImGuiRenderer::Render(const VisualState& vs, const RenderModel& rm, float viewportW, float viewportH)
	{
		(void)vs;
		(void)rm;
		(void)viewportW;
		(void)viewportH;
	}

	void AuthImGuiRenderer::SyncTransientFromModel(const VisualState&, const RenderModel&) {}
} // namespace engine::render

#endif
