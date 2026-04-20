#include "engine/render/AuthImGuiRenderer.h"

#include "engine/client/LocalizationService.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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

		void SpreadDigitsToVerifySlots(const std::string& s, char dst[7])
		{
			std::memset(dst, 0, 7u);
			int t = 0;
			for (unsigned char c : s)
			{
				if (t >= 6)
				{
					break;
				}
				if (c >= '0' && c <= '9')
				{
					dst[t++] = static_cast<char>(c);
				}
			}
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

		constexpr int kOptionsRes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
		constexpr int kOptionsResCount = sizeof(kOptionsRes) / sizeof(kOptionsRes[0]);

		int OptionsResolutionIndex(int w, int h)
		{
			for (int i = 0; i < kOptionsResCount; ++i)
			{
				if (kOptionsRes[i][0] == w && kOptionsRes[i][1] == h)
				{
					return i;
				}
			}
			int best = 2;
			int bestScore = 2147483647;
			for (int i = 0; i < kOptionsResCount; ++i)
			{
				const int s = std::abs(kOptionsRes[i][0] - w) + std::abs(kOptionsRes[i][1] - h);
				if (s < bestScore)
				{
					bestScore = s;
					best = i;
				}
			}
			return best;
		}

		std::string ShardEndpointHost(const std::string& endpoint)
		{
			if (endpoint.empty())
			{
				return {};
			}
			const auto colon = endpoint.find(':');
			return (colon == std::string::npos) ? endpoint : endpoint.substr(0u, colon);
		}

		char ShardInitialFromEndpoint(const std::string& endpoint)
		{
			const std::string host = ShardEndpointHost(endpoint);
			const std::string& scan = host.empty() ? endpoint : host;
			for (unsigned char c : scan)
			{
				if (std::isalpha(c) != 0)
				{
					return static_cast<char>(std::toupper(c));
				}
			}
			return '?';
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
		m_authErrorPillPreview = -1;
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
		m_authTweakPanelMinimized = false;
		m_regCountryComboIdx = 0;
		m_optResIdx = 2;
		m_optQualityPreset = 2;
		m_optFovDegrees = 70.f;
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
		m_optResIdx = OptionsResolutionIndex(m.videoResWidth, m.videoResHeight);
		m_optQualityPreset = static_cast<int>(std::clamp<int32_t>(m.videoQualityPreset, 0, 3));
		m_optFovDegrees = std::clamp(m.videoFovDegrees, 60.f, 120.f);
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

		if (vs.error)
		{
			m_authErrorPillPreview = -1;
		}

		if (vs.login && rm.fields.size() >= 1u)
		{
			StrCopyTrunc(m_loginId, sizeof(m_loginId), rm.fields[0].value);
			m_loginPw[0] = '\0';
			if (!rm.bodyLines.empty() && rm.bodyLines[0].checkbox)
			{
				m_rememberMe = rm.bodyLines[0].checkboxChecked;
			}
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
			if (!rm.authRegisterCountryPick.empty())
			{
				m_regCountryComboIdx = 0;
				for (size_t i = 0; i < rm.authRegisterCountryPick.size(); ++i)
				{
					const std::string& code = rm.authRegisterCountryPick[i].first;
					if (code.size() >= 2u && std::strncmp(m_regCountry, code.c_str(), 2) == 0)
					{
						m_regCountryComboIdx = static_cast<int>(i);
						break;
					}
				}
			}
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
			if (!rm.authRegisterCountryPick.empty())
			{
				m_regCountryComboIdx = 0;
				for (size_t i = 0; i < rm.authRegisterCountryPick.size(); ++i)
				{
					const std::string& code = rm.authRegisterCountryPick[i].first;
					if (code.size() >= 2u && std::strncmp(m_regCountry, code.c_str(), 2) == 0)
					{
						m_regCountryComboIdx = static_cast<int>(i);
						break;
					}
				}
			}
		}
		if (vs.verifyEmail && !vs.emailConfirmationPending)
		{
			if (!rm.fields.empty())
			{
				SpreadDigitsToVerifySlots(rm.fields[0].value, m_verifyCode);
			}
			else
			{
				std::memset(m_verifyCode, 0, sizeof(m_verifyCode));
			}
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

		const float overlayAlpha = (vs.languageSelection || vs.login || vs.registerMode || vs.error
									   || (vs.verifyEmail && !vs.emailConfirmationPending) || vs.options || vs.languageOptions
									   || vs.shardPick)
			? 0.22f
			: 1.f;
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

	void AuthImGuiRenderer::DrawAuthGoldField(const engine::client::AuthUiPresenter::RenderField& spec, char* buf, int bufSz,
		bool password)
	{
		std::string lab = spec.label;
		for (char& ch : lab)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
		ImGui::TextUnformatted(lab.c_str());
		ImGui::PopStyleColor();

		ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

		char inputId[64];
		std::snprintf(inputId, sizeof(inputId), "##gf_%p", static_cast<void*>(buf));
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
		if (password)
		{
			flags |= ImGuiInputTextFlags_Password;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		const char* hint = spec.inputPlaceholder.empty() ? nullptr : spec.inputPlaceholder.c_str();
		if (hint != nullptr && buf[0] == '\0')
		{
			ImGui::InputTextWithHint(inputId, hint, buf, static_cast<size_t>(bufSz), flags);
		}
		else
		{
			ImGui::InputText(inputId, buf, static_cast<size_t>(bufSz), flags);
		}

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		ImGui::Spacing();
	}

	void AuthImGuiRenderer::DrawLoginRememberRow(const RenderModel& rm)
	{
		const float trackW = 46.f;
		const float trackH = 22.f;
		const float pad = 3.f;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##remember_toggle", ImVec2(trackW + 4.f, trackH + 6.f));
		if (ImGui::IsItemClicked())
		{
			m_rememberMe = !m_rememberMe;
		}
		const bool on = m_rememberMe;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 a(p0.x + 2.f, p0.y + 3.f);
		const ImVec2 b(a.x + trackW, a.y + trackH);
		const ImU32 fillOff = IM_COL32(40, 48, 62, 255);
		const ImU32 fillOn = IM_COL32(72, 90, 48, 255);
		dl->AddRectFilled(a, b, on ? fillOn : fillOff, trackH * 0.5f);
		dl->AddRect(a, b, U32(LnTheme::kBorder), trackH * 0.5f, 0, 1.f);
		const float thumbR = (trackH - pad * 2.f) * 0.5f;
		const float cx = on ? (b.x - pad - thumbR) : (a.x + pad + thumbR);
		const float cy = (a.y + b.y) * 0.5f;
		dl->AddCircleFilled(ImVec2(cx, cy), thumbR, U32(LnTheme::kAccent));

		ImGui::SameLine(0.f, 12.f);
		ImGui::BeginGroup();
		std::string rememberTitle = (!rm.bodyLines.empty() && !rm.bodyLines[0].text.empty())
			? rm.bodyLines[0].text
			: std::string("SE SOUVENIR DE MOI");
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::TextUnformatted(rememberTitle.c_str());
		ImGui::PopStyleColor();
		if (!rm.authRememberDetailLine.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::TextWrapped("%s", rm.authRememberDetailLine.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}
		ImGui::EndGroup();
	}

	void AuthImGuiRenderer::DrawLoginFooterChips(const RenderModel& rm)
	{
		if (rm.authLoginFooterChips.empty())
		{
			DrawKeycapHints({{"Tab", "champ suivant"}, {"Entree", "se connecter"}, {"Echap", "quitter"}});
			return;
		}
		DrawFooterChipRow(rm.authLoginFooterChips);
	}

	void AuthImGuiRenderer::DrawFooterChipRow(const std::vector<std::pair<std::string, std::string>>& chips)
	{
		for (size_t ci = 0; ci < chips.size(); ++ci)
		{
			if (ci > 0u)
			{
				ImGui::SameLine(0.f, 8.f);
			}
			const auto& chip = chips[ci];
			const float keyW = ImGui::CalcTextSize(chip.first.c_str()).x + 16.f;
			const float descW = ImGui::CalcTextSize(chip.second.c_str()).x + 12.f;
			const float chipW = keyW + descW + 8.f;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
			char cid[48];
			std::snprintf(cid, sizeof(cid), "##fchip_%zu", ci);
			ImGui::BeginChild(cid, ImVec2(chipW, 40.f), true, ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::SetWindowFontScale(0.92f);
			ImGui::TextUnformatted(chip.first.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(0.f, 6.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.78f);
			ImGui::TextUnformatted(chip.second.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::EndChild();
		}
	}

	void AuthImGuiRenderer::DrawRegisterFlowHeader(const RenderModel& rm, float vpW)
	{
		if (rm.authRegisterCrumbLabels.empty())
		{
			return;
		}
		const int cur = rm.authRegisterCrumbCurrent;
		float totalW = 0.f;
		std::vector<std::string> segments;
		segments.reserve(rm.authRegisterCrumbLabels.size());
		for (size_t i = 0; i < rm.authRegisterCrumbLabels.size(); ++i)
		{
			char buf[160]{};
			std::snprintf(buf, sizeof(buf), "(%02u) %s", static_cast<unsigned>(i + 1u), rm.authRegisterCrumbLabels[i].c_str());
			segments.emplace_back(buf);
			totalW += ImGui::CalcTextSize(segments.back().c_str()).x;
			if (i + 1u < rm.authRegisterCrumbLabels.size())
			{
				totalW += ImGui::CalcTextSize(" \xE2\x80\x94 ").x;
			}
		}
		ImGui::SetCursorPosX((vpW - totalW) * 0.5f);
		for (size_t i = 0; i < segments.size(); ++i)
		{
			if (i > 0u)
			{
				ImGui::SameLine(0.f, 0.f);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::TextUnformatted(" \xE2\x80\x94 ");
				ImGui::PopStyleColor();
				ImGui::SameLine(0.f, 0.f);
			}
			const bool done = static_cast<int>(i) < cur;
			const bool active = static_cast<int>(i) == cur;
			const ImVec4 col = done ? IV(LnTheme::kSuccess) : (active ? IV(LnTheme::kAccent) : IV(LnTheme::kMuted));
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			if (active)
			{
				ImGui::SetWindowFontScale(1.04f);
			}
			ImGui::TextUnformatted(segments[i].c_str());
			if (active)
			{
				ImGui::SetWindowFontScale(1.f);
			}
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
	}

	void AuthImGuiRenderer::DrawRegisterFooterChips(const RenderModel& rm)
	{
		if (rm.authRegisterFooterChips.empty())
		{
			DrawKeycapHints({{"Entree", "valider"}, {"Echap", "retour"}});
			return;
		}
		DrawFooterChipRow(rm.authRegisterFooterChips);
	}

	void AuthImGuiRenderer::DrawAuthTweaksPanel(float vpW, float vpH)
	{
		static constexpr const char* kRaceLabels[] = {"DEFAUT", "HUMAINS", "ELFES", "NAINS", "ORCS", "MORTS-V.", "CORROM.",
			"DIVINS", "DEMONS"};
		const float winW = 272.f;
		if (m_authTweakPanelMinimized)
		{
			ImGui::SetNextWindowPos(ImVec2(vpW - winW - 22.f, vpH - 42.f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(winW, 36.f), ImGuiCond_Always);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
			ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.78f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::Begin("##ln_auth_tweaks_mini",
				nullptr,
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
					| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextUnformatted("TWEAKS");
			ImGui::PopStyleColor();
			ImGui::SameLine(0.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("+").x - 4.f);
			if (ImGui::SmallButton("+##tweak_expand"))
			{
				m_authTweakPanelMinimized = false;
			}
			ImGui::End();
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar(3);
			return;
		}

		ImGui::SetNextWindowPos(ImVec2(vpW - winW - 22.f, vpH - 228.f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(winW, 218.f), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 12.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.78f)));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::Begin("##ln_auth_tweaks",
			nullptr,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus);

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::TextUnformatted("TWEAKS");
		ImGui::PopStyleColor();
		ImGui::SameLine(0.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("-").x - 4.f);
		if (ImGui::SmallButton("-##tweak_min"))
		{
			m_authTweakPanelMinimized = true;
		}
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("THEME DE RACE");
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
		ImGui::TextUnformatted("FOND ANIME");
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

	void AuthImGuiRenderer::DrawBreadcrumb(const std::vector<std::string>& steps, int current)
	{
		const int total = static_cast<int>(steps.size());
		for (int i = 0; i < total; ++i)
		{
			const bool done = i < current;
			const bool active = i == current;
			const ImVec4 col = done ? IV(LnTheme::kSuccess) : (active ? IV(LnTheme::kAccent) : IV(LnTheme::kMuted));
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::Text("%02d %s", i + 1, steps[static_cast<size_t>(i)].c_str());
			ImGui::PopStyleColor();
			if (i + 1 < total)
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

		DrawAuthTweaksPanel(vpW, vpH);
	}

	void AuthImGuiRenderer::RenderLoginScreen(const RenderModel& rm, float vpW, float vpH)
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

		std::string panelTitle = rm.sectionTitle.empty() ? std::string("CONNEXION") : rm.sectionTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string ver =
			rm.authLoginVersionBadge.empty() ? std::string("v0.0.0") : rm.authLoginVersionBadge;
		if (!BeginPanel(520.f, vpW, vpH, panelTitle, "", ver, true, false))
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

		if (rm.fields.size() >= 2u)
		{
			DrawAuthGoldField(rm.fields[0], m_loginId, static_cast<int>(sizeof(m_loginId)), false);
			DrawAuthGoldField(rm.fields[1], m_loginPw, static_cast<int>(sizeof(m_loginPw)), true);
		}
		else
		{
			DrawField("Identifiant", m_loginId, static_cast<int>(sizeof(m_loginId)), false);
			DrawField("Mot de passe", m_loginPw, static_cast<int>(sizeof(m_loginPw)), true);
		}

		DrawLoginRememberRow(rm);
		ImGui::Spacing();

		if (rm.bodyLines.size() >= 2u && rm.bodyLines[1].link && m_authPresenter != nullptr)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			if (ImGui::SmallButton(rm.bodyLines[1].text.c_str()))
			{
				m_authPresenter->ImGuiNavigateToForgotFromLogin();
			}
			ImGui::PopStyleColor();
			ImGui::SameLine(0.f, 12.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			if (ImGui::SmallButton("Portail web##forgot_portal") && m_authPresenter != nullptr && m_authCfg != nullptr
				&& m_authWindow != nullptr)
			{
				m_authPresenter->ImGuiOpenForgotPasswordPortal(*m_authCfg, *m_authWindow);
			}
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		const engine::client::AuthUiPresenter::RenderAction* actCreate = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actSubmit = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actOpts = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actQuit = nullptr;
		if (rm.actions.size() >= 4u)
		{
			actCreate = &rm.actions[0];
			actOpts = &rm.actions[1];
			actSubmit = &rm.actions[2];
			actQuit = &rm.actions[3];
		}

		const float rowBtnH = 34.f;
		if (actCreate != nullptr && actSubmit != nullptr)
		{
			const float gap = 12.f;
			const float submitW = 220.f;
			const float availRow = ImGui::GetContentRegionAvail().x;
			const float badgeCreateW =
				actCreate->actionBadge.empty() ? 0.f : (ImGui::CalcTextSize(actCreate->actionBadge.c_str()).x + 10.f);
			const float badgeSubmitW =
				actSubmit->actionBadge.empty() ? 0.f : (ImGui::CalcTextSize(actSubmit->actionBadge.c_str()).x + 10.f);
			const float createW =
				(std::max)(120.f, availRow - submitW - gap - badgeCreateW - badgeSubmitW - 6.f);
			ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.1f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.16f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kText));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			std::string createLabel = actCreate->label.empty() ? std::string("CRÉER UN COMPTE") : actCreate->label;
			createLabel += "##login_create";
			if (ImGui::Button(createLabel.c_str(), ImVec2(createW, rowBtnH)) && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiNavigateToRegisterFromLogin();
			}
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			if (!actCreate->actionBadge.empty())
			{
				ImGui::SameLine(0.f, 6.f);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.85f);
				ImGui::TextUnformatted(actCreate->actionBadge.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
			}
			ImGui::SameLine(0.f, gap);
			ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.39f, 0.58f, 0.82f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.38f, 0.62f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			std::string submitLabel = actSubmit->label.empty() ? std::string("SE CONNECTER") : actSubmit->label;
			submitLabel += "##login_submit";
			if (ImGui::Button(submitLabel.c_str(), ImVec2(submitW, rowBtnH)) && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				m_authPresenter->ImGuiSubmitLogin(*m_authCfg, m_loginId, m_loginPw, m_rememberMe);
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(4);
			if (!actSubmit->actionBadge.empty())
			{
				ImGui::SameLine(0.f, 6.f);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.95f);
				ImGui::TextUnformatted(actSubmit->actionBadge.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
			}
		}
		else if (DrawPrimaryButton("Se connecter") && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitLogin(*m_authCfg, m_loginId, m_loginPw, m_rememberMe);
		}

		DrawSeparator();
		DrawLoginFooterChips(rm);

		EndPanel();

		DrawAuthTweaksPanel(vpW, vpH);

		if (actOpts != nullptr && actQuit != nullptr && m_authPresenter != nullptr)
		{
			const std::string lo = actOpts->label.empty() ? std::string("OPTIONS") : actOpts->label;
			const std::string lq = actQuit->label.empty() ? std::string("QUITTER") : actQuit->label;
			const float tw = ImGui::CalcTextSize(lo.c_str()).x + ImGui::CalcTextSize(lq.c_str()).x + 48.f;
			ImGui::SetCursorPos(ImVec2((vpW - tw) * 0.5f, ImGui::GetCursorPosY() + 14.f));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			if (ImGui::SmallButton((lo + "##foot_opts").c_str()))
			{
				m_authPresenter->ImGuiOpenLanguageOptionsMenu();
			}
			ImGui::SameLine(0.f, 24.f);
			if (ImGui::SmallButton((lq + "##foot_quit").c_str()) && m_authWindow != nullptr)
			{
				m_authPresenter->ImGuiRequestClose(*m_authWindow);
			}
			ImGui::PopStyleColor();
		}
	}

	void AuthImGuiRenderer::RenderRegisterScreen(const RenderModel& rm, float vpW, float vpH)
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

		std::string panelTitle = rm.sectionTitle.empty() ? std::string("CRÉER UN COMPTE") : rm.sectionTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string& sub =
			rm.authRegisterPanelSubtitle.empty() ? std::string("FORGER VOTRE IDENTITÉ") : rm.authRegisterPanelSubtitle;
		const std::string ver =
			rm.authRegisterPanelBadge.empty() ? std::string("2 / 4") : rm.authRegisterPanelBadge;
		if (!BeginPanel(760.f, vpW, vpH, panelTitle, sub, ver, true, false))
		{
			EndPanel();
			return;
		}

		if (!rm.errorText.empty())
		{
			DrawBanner("Echec", rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);
		}

		const bool haveModel = (rm.fields.size() >= 10u && rm.dropdowns.size() >= 3u);
		if (haveModel)
		{
			DrawAuthGoldField(rm.fields[0], m_regId, static_cast<int>(sizeof(m_regId)), false);
			DrawAuthGoldField(rm.fields[4], m_regEmail, static_cast<int>(sizeof(m_regEmail)), false);
			if (!rm.authRegisterEmailHint.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				ImGui::TextWrapped("%s", rm.authRegisterEmailHint.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}

			const float halfW = (ImGui::GetContentRegionAvail().x - 14.f) * 0.5f;
			ImGui::Columns(2, "##reg_pw_col", false);
			ImGui::SetColumnWidth(0, halfW);
			ImGui::SetColumnWidth(1, halfW);
			DrawAuthGoldField(rm.fields[8], m_regPw, static_cast<int>(sizeof(m_regPw)), true);
			ImGui::NextColumn();
			DrawAuthGoldField(rm.fields[9], m_regPw2, static_cast<int>(sizeof(m_regPw2)), true);
			ImGui::Columns(1);

			auto buildDdPtrs = [](const engine::client::AuthUiPresenter::RenderDropdown& dd, std::vector<std::string>& store,
								   std::vector<const char*>& ptrs) {
				store.clear();
				ptrs.clear();
				for (const auto& o : dd.options)
				{
					store.push_back(o.label);
					ptrs.push_back(store.back().c_str());
				}
			};
			std::vector<std::string> dayStore;
			std::vector<const char*> dayPtrs;
			std::vector<std::string> monStore;
			std::vector<const char*> monPtrs;
			std::vector<std::string> yrStore;
			std::vector<const char*> yrPtrs;
			buildDdPtrs(rm.dropdowns[0], dayStore, dayPtrs);
			buildDdPtrs(rm.dropdowns[1], monStore, monPtrs);
			buildDdPtrs(rm.dropdowns[2], yrStore, yrPtrs);

			ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			const float halfDate = (ImGui::GetContentRegionAvail().x - 10.f) * 0.5f;
			ImGui::SetNextItemWidth(halfDate);
			if (!dayPtrs.empty())
			{
				ImGui::Combo("##reg_day", &m_regBirthDayIdx, dayPtrs.data(), static_cast<int>(dayPtrs.size()));
			}
			ImGui::SameLine(0.f, 10.f);
			ImGui::SetNextItemWidth(halfDate);
			if (!monPtrs.empty())
			{
				ImGui::Combo("##reg_month", &m_regBirthMonthIdx, monPtrs.data(), static_cast<int>(monPtrs.size()));
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (!yrPtrs.empty())
			{
				ImGui::Combo("##reg_year", &m_regBirthYearIdx, yrPtrs.data(), static_cast<int>(yrPtrs.size()));
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(2);
			ImGui::Spacing();

			{
				std::string lab = rm.fields[1].label;
				for (char& ch : lab)
				{
					if (ch >= 'a' && ch <= 'z')
					{
						ch = static_cast<char>(ch - 'a' + 'A');
					}
				}
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
				ImGui::TextUnformatted(lab.c_str());
				ImGui::PopStyleColor();
				if (!rm.authRegisterCountryPick.empty())
				{
					m_regCountryComboIdx =
						std::clamp(m_regCountryComboIdx, 0, static_cast<int>(rm.authRegisterCountryPick.size()) - 1);
					const std::string& preview = rm.authRegisterCountryPick[static_cast<size_t>(m_regCountryComboIdx)].second;
					ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
					ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
					if (ImGui::BeginCombo("##reg_country", preview.c_str()))
					{
						for (int i = 0; i < static_cast<int>(rm.authRegisterCountryPick.size()); ++i)
						{
							const bool sel = (i == m_regCountryComboIdx);
							ImGui::PushID(i);
							if (ImGui::Selectable(rm.authRegisterCountryPick[static_cast<size_t>(i)].second.c_str(), sel))
							{
								m_regCountryComboIdx = i;
							}
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}
					ImGui::PopStyleVar(1);
					ImGui::PopStyleColor(2);
					const std::string& iso = rm.authRegisterCountryPick[static_cast<size_t>(m_regCountryComboIdx)].first;
					if (iso.size() >= 2u)
					{
						m_regCountry[0] = iso[0];
						m_regCountry[1] = iso[1];
						m_regCountry[2] = '\0';
					}
				}
				else
				{
					DrawField("Pays (ISO2)", m_regCountry, static_cast<int>(sizeof(m_regCountry)), false);
				}
				ImGui::Spacing();
			}

			const float nameW = (ImGui::GetContentRegionAvail().x - 12.f) * 0.5f;
			ImGui::Columns(2, "##reg_name_col", false);
			ImGui::SetColumnWidth(0, nameW);
			ImGui::SetColumnWidth(1, nameW);
			DrawAuthGoldField(rm.fields[2], m_regLastName, static_cast<int>(sizeof(m_regLastName)), false);
			ImGui::NextColumn();
			DrawAuthGoldField(rm.fields[3], m_regFirstName, static_cast<int>(sizeof(m_regFirstName)), false);
			ImGui::Columns(1);
		}
		else
		{
			DrawField("Identifiant", m_regId, static_cast<int>(sizeof(m_regId)));
			DrawField("Courriel", m_regEmail, static_cast<int>(sizeof(m_regEmail)));
			DrawField("Prenom", m_regFirstName, static_cast<int>(sizeof(m_regFirstName)));
			DrawField("Nom", m_regLastName, static_cast<int>(sizeof(m_regLastName)));
			DrawField("Pays", m_regCountry, static_cast<int>(sizeof(m_regCountry)));
		}

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

		std::string dayStr = "01";
		std::string monStr = "01";
		std::string yrStr = "2000";
		if (rm.dropdowns.size() > 2u)
		{
			const int di = std::clamp(m_regBirthDayIdx, 0, static_cast<int>(rm.dropdowns[0].options.size()) - 1);
			const int mi = std::clamp(m_regBirthMonthIdx, 0, static_cast<int>(rm.dropdowns[1].options.size()) - 1);
			const int yi = std::clamp(m_regBirthYearIdx, 0, static_cast<int>(rm.dropdowns[2].options.size()) - 1);
			auto pad2 = [](std::string s) -> std::string {
				if (s.size() == 1u)
				{
					return std::string("0") + s;
				}
				return s;
			};
			dayStr = pad2(rm.dropdowns[0].options[static_cast<size_t>(di)].value);
			monStr = pad2(rm.dropdowns[1].options[static_cast<size_t>(mi)].value);
			yrStr = rm.dropdowns[2].options[static_cast<size_t>(yi)].value;
		}

		const bool fieldsOk = std::strlen(m_regId) > 0 && std::strlen(m_regEmail) > 0 && std::strlen(m_regFirstName) > 0
			&& std::strlen(m_regLastName) > 0 && std::strlen(m_regCountry) >= 2u;
		const bool canSubmit = fieldsOk && (strength >= 3) && (std::strlen(m_regPw) > 0) && (std::strcmp(m_regPw, m_regPw2) == 0);

		const engine::client::AuthUiPresenter::RenderAction* actSubmit = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actBack = nullptr;
		if (rm.actions.size() >= 2u)
		{
			actSubmit = &rm.actions[0];
			actBack = &rm.actions[1];
		}

		const float backW = 200.f;
		const float submitW = 280.f;
		const float rowH = 34.f;
		if (actBack != nullptr && actSubmit != nullptr)
		{
			std::string backLab = actBack->label.empty() ? std::string("RETOUR") : actBack->label;
			backLab += "##reg_back";
			ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.1f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.16f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			if (ImGui::Button(backLab.c_str(), ImVec2(backW, rowH)) && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiBackFromRegisterToLogin();
			}
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			if (!actBack->actionBadge.empty())
			{
				ImGui::SameLine(0.f, 6.f);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.85f);
				ImGui::TextUnformatted(actBack->actionBadge.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
			}
			if (!rm.authRegisterShowErrorsLabel.empty() && !canSubmit)
			{
				ImGui::SameLine(0.f, 18.f);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				if (ImGui::SmallButton(rm.authRegisterShowErrorsLabel.c_str()))
				{
				}
				ImGui::PopStyleColor();
			}

			const float submitBadgeW =
				actSubmit->actionBadge.empty() ? 0.f : (ImGui::CalcTextSize(actSubmit->actionBadge.c_str()).x + 10.f);
			const float leftEndX = ImGui::GetItemRectMax().x;
			float rowRight = ImGui::GetCursorStartPos().x + ImGui::GetContentRegionAvail().x - submitW - submitBadgeW - 4.f;
			if (rowRight < leftEndX + 12.f)
			{
				rowRight = leftEndX + 12.f;
			}
			ImGui::SetCursorPosX(rowRight);
			std::string subLab = actSubmit->label.empty() ? std::string("CRÉER LE COMPTE") : actSubmit->label;
			subLab += "##reg_submit";
			ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.39f, 0.58f, 0.82f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.38f, 0.62f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			if (ImGui::Button(subLab.c_str(), ImVec2(submitW, rowH)) && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				engine::client::AuthUiPresenter::RegisterImGuiSubmit form{};
				form.login = m_regId;
				form.email = m_regEmail;
				form.password = m_regPw;
				form.passwordConfirm = m_regPw2;
				form.firstName = m_regFirstName;
				form.lastName = m_regLastName;
				form.birthDay = dayStr.c_str();
				form.birthMonth = monStr.c_str();
				form.birthYear = yrStr.c_str();
				form.countryIso2 = m_regCountry;
				m_authPresenter->ImGuiSubmitRegister(*m_authCfg, form);
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(4);
			if (!actSubmit->actionBadge.empty())
			{
				ImGui::SameLine(0.f, 6.f);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.95f);
				ImGui::TextUnformatted(actSubmit->actionBadge.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
			}
			ImGui::Spacing();
		}
		else
		{
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
				form.birthDay = dayStr.c_str();
				form.birthMonth = monStr.c_str();
				form.birthYear = yrStr.c_str();
				form.countryIso2 = m_regCountry;
				m_authPresenter->ImGuiSubmitRegister(*m_authCfg, form);
			}
		}

		DrawSeparator();
		DrawRegisterFooterChips(rm);
		EndPanel();

		DrawAuthTweaksPanel(vpW, vpH);
	}

	void AuthImGuiRenderer::RenderErrorScreen(const RenderModel& rm, float vpW, float vpH)
	{
		auto acknowledge = [this]() {
			if (m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				m_authPresenter->ImGuiAcknowledgeErrorScreen(*m_authCfg);
			}
		};

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
			const std::string& ver =
				rm.authErrorVersionBadge.empty() ? std::string("Erreur") : rm.authErrorVersionBadge;
			if (!BeginPanel(640.f, vpW, vpH, panelTitle, "", ver, true, false))
			{
				EndPanel();
				DrawAuthTweaksPanel(vpW, vpH);
				return;
			}

			const int n = static_cast<int>(rm.authRegisterErrorVariants.size());
			const int classified = std::clamp(rm.authRegisterErrorClassifiedIndex, 0, n - 1);
			const int shown = (m_authErrorPillPreview >= 0) ? std::clamp(m_authErrorPillPreview, 0, n - 1) : classified;
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

			const auto& v = rm.authRegisterErrorVariants[static_cast<size_t>(shown)];
			const LnTheme::Rgba bannerRgb = v.warningBanner ? LnTheme::kWarning : LnTheme::kErrorCol;
			const bool rawBody = rm.authErrorBannerBodyFromUserMessage && shown == classified;
			const std::string& bodyMsg = rawBody ? rm.errorText : v.bannerMessage;
			DrawBanner(v.bannerTitle, bodyMsg, bannerRgb.r, bannerRgb.g, bannerRgb.b);

			if (!v.fieldLabel.empty() && !rm.authErrorHideFieldBox)
			{
				ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kSurface));
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
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
			const ImVec2 wpos = ImGui::GetWindowPos();
			const float fixH = ImGui::GetFontSize() * 4.f + 24.f;
			ImGui::GetWindowDrawList()->AddRectFilled(wpos, ImVec2(wpos.x + 3.f, wpos.y + fixH), U32(LnTheme::kAccent));
			ImGui::SetCursorPosX(8.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::SetWindowFontScale(0.78f);
			const std::string& hfix =
				rm.authErrorFixSectionLabel.empty() ? std::string("COMMENT CORRIGER") : rm.authErrorFixSectionLabel;
			ImGui::TextUnformatted(hfix.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::SetCursorPosX(8.f);
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
		DrawBanner(banTitle, rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);

		const std::string& backLbl =
			rm.authErrorBackButtonLabel.empty() ? std::string("RETOUR AU FORMULAIRE") : rm.authErrorBackButtonLabel;
		if (DrawGhostButton(backLbl.c_str()) && m_authPresenter != nullptr && m_authCfg != nullptr)
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

		const std::string& digitLab =
			rm.authVerifyDigitLabel.empty() ? std::string("CODE DE VERIFICATION") : rm.authVerifyDigitLabel;
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.78f);
		ImGui::TextUnformatted(digitLab.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const float boxW = 48.f;
		const float gap = 8.f;
		const float rowW = boxW * 6.f + gap * 5.f;
		ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - rowW) * 0.5f + ImGui::GetCursorPosX());

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 14.f));
		for (int i = 0; i < 6; ++i)
		{
			if (i > 0)
			{
				ImGui::SameLine(0.f, gap);
			}
			ImGui::PushID(i);
			char one[8]{};
			if (m_verifyCode[i] >= '0' && m_verifyCode[i] <= '9')
			{
				one[0] = m_verifyCode[i];
			}
			ImGui::SetNextItemWidth(boxW);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			if (ImGui::InputText("##vd", one, sizeof(one), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll))
			{
				const char c = one[0];
				if (c >= '0' && c <= '9')
				{
					m_verifyCode[i] = c;
				}
				else
				{
					m_verifyCode[i] = '\0';
				}
			}
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
			ImGui::PopID();
		}
		ImGui::PopStyleVar(1);

		if (m_authPresenter != nullptr)
		{
			const std::string packed = PackVerifySlotsInOrder(m_verifyCode);
			m_authPresenter->ImGuiSetVerifyEmailPartialCode(packed);
		}

		ImGui::Spacing();
		const std::string& resend =
			rm.authVerifyResendLabel.empty() ? std::string("Renvoyer le code") : rm.authVerifyResendLabel;
		const std::string& chmail =
			rm.authVerifyChangeEmailLabel.empty() ? std::string("Modifier le courriel") : rm.authVerifyChangeEmailLabel;
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
		const auto tr = [this](const char* key) -> std::string {
			if (m_authPresenter == nullptr)
			{
				return std::string(key);
			}
			std::string s = m_authPresenter->UiTranslate(key);
			return s.empty() ? std::string(key) : s;
		};

		const float sideW = 240.f;
		const float mainW = (std::max)(200.f, vpW - sideW);
		const float height = vpH;

		static constexpr const char* kTabKeys[] = {"options.imgui.tab.graphics", "options.imgui.tab.audio", "options.imgui.tab.controls",
			"options.imgui.tab.lang", "options.imgui.tab.ui", "options.imgui.tab.net", "options.imgui.tab.account"};
		static constexpr const char* kTabIcons[] = {"\xE2\x96\xA3", "\xE2\x99\xAB", "\xE2\x8C\xA8", "Aa", "\xE2\x8C\x97", "\xE2\x8C\x98", "\xE2\x9C\xA6"};
		static constexpr int tabCount = 7;

		ImGui::SetCursorPos(ImVec2(0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kPanel));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::BeginChild("##opts_sidebar", ImVec2(sideW, height), true, ImGuiWindowFlags_None);
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(2);

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(1.08f);
		{
			const std::string sideTitle = tr("options.imgui.sidebar_title");
			ImGui::TextUnformatted(sideTitle.c_str());
		}
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		DrawSeparator();

		for (int i = 0; i < tabCount; ++i)
		{
			const bool active = (m_optionsTab == i);
			const std::string tabLabel = tr(kTabKeys[i]);
			ImGui::PushStyleColor(ImGuiCol_Button, active ? IV(LnTheme::AccentDim(0.12f)) : ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_Text, active ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
			ImGui::PushStyleColor(ImGuiCol_Border, active ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, active ? 1.5f : 0.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
			char btnLine[192];
			std::snprintf(btnLine, sizeof(btnLine), "%s  %s##tab%d", kTabIcons[i], tabLabel.c_str(), i);
			if (ImGui::Button(btnLine, ImVec2(-FLT_MIN, 36.f)))
			{
				m_optionsTab = i;
			}
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
		}

		const float footerReserve = 56.f;
		const float spacerH = ImGui::GetContentRegionAvail().y - footerReserve;
		if (spacerH > 2.f)
		{
			ImGui::Dummy(ImVec2(1.f, spacerH));
		}
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.82f);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.92f);
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + sideW - 24.f);
		ImGui::TextWrapped("%s", tr("options.imgui.sidebar_footer").c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleVar(1);
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		ImGui::EndChild();

		ImGui::SetCursorPos(ImVec2(sideW, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::kBackground));
		ImGui::BeginChild("##opts_main", ImVec2(mainW, height), false, ImGuiWindowFlags_None);
		ImGui::PopStyleColor(1);

		ImGui::BeginChild("##opts_header", ImVec2(-FLT_MIN, 0.f), false);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.78f);
		ImGui::TextUnformatted(tr("options.imgui.category_label").c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::SetWindowFontScale(1.12f);
		{
			const std::string mainTabTitle = tr(kTabKeys[m_optionsTab]);
			ImGui::TextUnformatted(mainTabTitle.c_str());
		}
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		if (m_optDirty)
		{
			ImGui::SameLine(0.f, 18.f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kWarning));
			ImGui::TextUnformatted(tr("options.imgui.dirty_banner_title").c_str());
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		DrawSeparator();

		const float footerH = 52.f;
		ImGui::BeginChild("##opts_body", ImVec2(-FLT_MIN, (std::max)(120.f, height - footerH - 88.f)), false);

		const auto markDirty = [this]() { m_optDirty = true; };

		const auto sliderVol01 = [&](const char* label, float* v01) {
			float pct = *v01 * 100.f;
			if (ImGui::SliderFloat(label, &pct, 0.f, 100.f, "%.0f%%"))
			{
				*v01 = pct * 0.01f;
				markDirty();
			}
		};

		const auto sectionTitle = [&](const char* key) {
			ImGui::Spacing();
			const std::string t = tr(key);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::TextUnformatted(t.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::Spacing();
		};

		const auto hintLine = [&](const char* key) {
			const std::string h = tr(key);
			if (h.empty())
			{
				return;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.78f);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.9f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
			ImGui::TextWrapped("%s", h.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleVar(1);
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		};

		const auto toggleRow = [&](const char* labelKey, bool* v, const char* hintKey) {
			const float fullW = ImGui::GetContentRegionAvail().x;
			const std::string lbl = tr(labelKey);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IV(LnTheme::AccentDim(0.12f)));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, IV(LnTheme::kAccent));
			ImGui::BeginGroup();
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(lbl.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(fullW - 28.f);
			char cbId[96];
			std::snprintf(cbId, sizeof(cbId), "##cb_%s", labelKey);
			if (ImGui::Checkbox(cbId, v))
			{
				markDirty();
			}
			ImGui::EndGroup();
			ImGui::PopStyleColor(3);
			if (hintKey != nullptr && hintKey[0] != '\0')
			{
				hintLine(hintKey);
			}
			ImGui::Spacing();
		};

		const auto submitOptionsMirror = [&]() {
			if (m_authPresenter == nullptr || m_authCfg == nullptr)
			{
				return;
			}
			m_optResIdx = std::clamp(m_optResIdx, 0, kOptionsResCount - 1);
			engine::client::AuthUiPresenter::LanguageOptionsImGuiMirror mir{};
			mir.videoFullscreen = m_optFullscreen;
			mir.videoVsync = m_optVsync;
			mir.videoResWidth = kOptionsRes[m_optResIdx][0];
			mir.videoResHeight = kOptionsRes[m_optResIdx][1];
			mir.videoQualityPreset = static_cast<int32_t>(std::clamp(m_optQualityPreset, 0, 3));
			mir.videoFovDegrees = std::clamp(m_optFovDegrees, 60.f, 120.f);
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
		};

		ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IV(LnTheme::AccentDim(0.12f)));
		ImGui::PushStyleColor(ImGuiCol_SliderGrab, IV(LnTheme::kAccent));
		ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IV(LnTheme::kAccent));

		if (m_optionsTab == 0)
		{
			sectionTitle("options.imgui.section.display");
			const char* resItems = "1280x720\01600x900\01920x1080\02560x1440\03840x2160\0\0";
			const std::string resLab = tr("options.imgui.resolution");
			if (ImGui::Combo(resLab.c_str(), &m_optResIdx, resItems))
			{
				m_optResIdx = std::clamp(m_optResIdx, 0, kOptionsResCount - 1);
				markDirty();
			}
			std::string qualityItems;
			for (int q = 0; q < 4; ++q)
			{
				char qk[56];
				std::snprintf(qk, sizeof(qk), "options.imgui.quality.%d", q);
				qualityItems += tr(qk);
				qualityItems.push_back('\0');
			}
			qualityItems.push_back('\0');
			const std::string qualLab = tr("options.imgui.quality");
			if (ImGui::Combo(qualLab.c_str(), &m_optQualityPreset, qualityItems.c_str()))
			{
				m_optQualityPreset = std::clamp(m_optQualityPreset, 0, 3);
				markDirty();
			}
			hintLine("options.imgui.quality_hint");

			ImGui::Spacing();
			const std::string fovLabel = tr("options.imgui.fov");
			if (ImGui::SliderFloat(fovLabel.c_str(), &m_optFovDegrees, 60.f, 120.f, "%.0f \xC2\xB0"))
			{
				m_optFovDegrees = std::clamp(m_optFovDegrees, 60.f, 120.f);
				markDirty();
			}

			sectionTitle("options.imgui.section.modes");
			toggleRow("options.imgui.fullscreen", &m_optFullscreen, "options.imgui.fullscreen_hint");
			toggleRow("options.imgui.vsync", &m_optVsync, "options.imgui.vsync_hint");
		}
		else if (m_optionsTab == 1)
		{
			sectionTitle("options.imgui.section.volumes");
			sliderVol01(tr("options.audio.master").c_str(), &m_optAudioMaster);
			sliderVol01(tr("options.audio.music").c_str(), &m_optAudioMusic);
			sliderVol01(tr("options.audio.sfx").c_str(), &m_optAudioSfx);
			sliderVol01(tr("options.audio.ui").c_str(), &m_optAudioUi);
		}
		else if (m_optionsTab == 2)
		{
			sectionTitle("options.imgui.section.mouse_keyboard");
			float sensUi = m_optMouseSens * 1000.f;
			if (ImGui::SliderFloat(tr("options.controls.mouse_sensitivity").c_str(), &sensUi, 1.f, 10.f, "%.2f"))
			{
				m_optMouseSens = sensUi * 0.001f;
				markDirty();
			}
			toggleRow("options.controls.invert_y", &m_optInvertY, "options.imgui.hint.invert_y");
			toggleRow("options.imgui.zqsd_layout", &m_optUseZqsd, "options.imgui.hint.zqsd");
		}
		else if (m_optionsTab == 3)
		{
			sectionTitle("options.imgui.tab.lang");
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
					const std::string langComboLabel = tr("options.imgui.lang_interface");
					if (ImGui::Combo(langComboLabel.c_str(), &m_optLangIndex, ptrs.data(), n))
					{
						markDirty();
					}
				}
			}
			else
			{
				ImGui::TextDisabled("(Presenter)");
			}
		}
		else if (m_optionsTab == 4)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", tr("options.imgui.ui_placeholder").c_str());
			ImGui::PopStyleColor();
		}
		else if (m_optionsTab == 5)
		{
			sectionTitle("options.imgui.section.network");
			toggleRow("options.game.gameplay_udp", &m_optGameplayUdp, "options.imgui.hint.gameplay_udp");
			toggleRow("options.game.allow_insecure_dev", &m_optAllowInsecureDev, "options.imgui.hint.allow_insecure");
			int tmo = static_cast<int>(m_optAuthTimeoutMs);
			if (ImGui::SliderInt(tr("options.game.auth_timeout").c_str(), &tmo, 1000, 15000, "%d ms"))
			{
				m_optAuthTimeoutMs = static_cast<uint32_t>(tmo);
				markDirty();
			}
		}
		else if (m_optionsTab == 6)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", tr("options.imgui.account_placeholder").c_str());
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleColor(4);

		ImGui::EndChild();

		DrawSeparator();
		const float btnW = 128.f;
		const std::string backStr = tr("options.imgui.button.back");
		const std::string cancelStr = tr("common.cancel");
		const std::string applyStr = tr("common.apply");

		const auto drawSizedGhost = [&](const char* label, float w, bool disabled) -> bool {
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
			const bool c = ImGui::Button(label, ImVec2(w, 32.f));
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			if (disabled)
			{
				ImGui::EndDisabled();
			}
			return c;
		};

		if (drawSizedGhost(backStr.c_str(), btnW, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCloseLanguageOptionsWithoutApply();
		}
		ImGui::SameLine(0.f, 10.f);
		if (drawSizedGhost(cancelStr.c_str(), btnW, !m_optDirty) && m_authPresenter != nullptr)
		{
			PullLanguageOptionsFromPresenter();
		}
		{
			const float applyPosX = ImGui::GetWindowContentRegionMax().x - btnW;
			ImGui::SetCursorPosX(applyPosX);
		}
		if (!m_optDirty)
		{
			ImGui::BeginDisabled();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kAccent));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
		}
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.25f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.35f)));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		const bool applyClick = ImGui::Button(applyStr.c_str(), ImVec2(btnW, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (!m_optDirty)
		{
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
		}
		if (applyClick)
		{
			submitOptionsMirror();
		}

		ImGui::EndChild();

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCloseLanguageOptionsWithoutApply();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && m_optDirty)
		{
			submitOptionsMirror();
		}

		DrawAuthTweaksPanel(vpW, vpH);
	}

	void AuthImGuiRenderer::RenderShardScreen(const RenderModel& rm, float vpW, float vpH)
	{
		using P = engine::client::LocalizationService::Params;
		auto tr = [this](const char* key, const P& p = {}) -> std::string {
			if (m_authPresenter == nullptr)
			{
				return std::string(key);
			}
			std::string s = m_authPresenter->UiTranslate(key, p);
			return s.empty() ? std::string(key) : s;
		};

		const std::vector<std::string> breadcrumb = {tr("auth.shard_pick.breadcrumb.account"), tr("auth.shard_pick.breadcrumb.realm"),
			tr("auth.shard_pick.breadcrumb.character"), tr("auth.shard_pick.breadcrumb.enter")};
		DrawBreadcrumb(breadcrumb, 1);

		const std::string titleStr = rm.sectionTitle.empty() ? tr("auth.shard_pick.panel_title") : rm.sectionTitle;
		const std::string subStr = tr("auth.shard_pick.panel_subtitle");

		uint32_t choice = 0u;
		static const std::vector<engine::network::ServerListEntry> kEmptyShardList{};
		const std::vector<engine::network::ServerListEntry>& entries =
			m_authPresenter != nullptr ? m_authPresenter->ShardPickEntries() : kEmptyShardList;
		if (m_authPresenter != nullptr)
		{
			choice = m_authPresenter->ShardPickChoiceShardId();
		}

		size_t onlineCount = 0u;
		for (const auto& e : entries)
		{
			if (e.status == 1u || e.status == 2u)
			{
				++onlineCount;
			}
		}
		const std::string verStr = tr("auth.shard_pick.version_online",
			P{{"online", std::to_string(onlineCount)}, {"total", std::to_string(entries.size())}});

		if (!BeginPanel(820.f, vpW, vpH, std::string_view(titleStr), std::string_view(subStr), std::string_view(verStr), true, false))
		{
			EndPanel();
			return;
		}

		const std::string infoStr = tr("auth.shard_pick.footer_info");
		if (!infoStr.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 760.f);
			ImGui::TextWrapped("%s", infoStr.c_str());
			ImGui::PopTextWrapPos();
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		const float listH = (std::max)(200.f, vpH * 0.42f);
		ImGui::BeginChild("##shard_scroll", ImVec2(-FLT_MIN, listH), true, ImGuiWindowFlags_None);

		if (entries.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", tr("auth.shard_pick.none_online").c_str());
			ImGui::PopStyleColor();
		}
		else
		{
			constexpr float kFlag = 50.f;
			constexpr float kLoadW = 200.f;
			constexpr float kPingW = 112.f;
			constexpr float kRowH = 114.f;

			for (const auto& e : entries)
			{
				const bool rowSelectable = (e.status == 1u && !e.endpoint.empty());
				const bool isSelected = (choice == e.shard_id);
				const float loadFrac = e.max_capacity > 0u
					? static_cast<float>(e.current_load) / static_cast<float>(e.max_capacity)
					: 0.f;
				const int pct = static_cast<int>(std::lround(loadFrac * 100.f));
				const bool saturated = rowSelectable && loadFrac > 0.85f;

				enum class RowVis : uint8_t
				{
					Offline,
					Saturated,
					Online
				};
				RowVis vis = RowVis::Offline;
				if (rowSelectable)
				{
					vis = saturated ? RowVis::Saturated : RowVis::Online;
				}
				else if (e.status == 2u)
				{
					vis = RowVis::Saturated;
				}

				const std::string host = ShardEndpointHost(e.endpoint);
				std::string nameUpper = host.empty()
					? tr("auth.shard_pick.name_fallback", P{{"id", std::to_string(e.shard_id)}})
					: host;
				if (!host.empty())
				{
					for (char& ch : nameUpper)
					{
						if (ch >= 'a' && ch <= 'z')
						{
							ch = static_cast<char>(ch - 'a' + 'A');
						}
					}
				}
				const char initialBuf[4] = {ShardInitialFromEndpoint(e.endpoint), '\0', '\0', '\0'};
				const std::string descLine =
					e.endpoint.empty() ? tr("auth.shard_pick.desc_offline") : e.endpoint;

				const ImVec4 borderCol = isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder);
				const float dim = (rowSelectable || e.status == 2u) ? 1.f : 0.48f;

				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * dim);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, isSelected ? IV(LnTheme::AccentDim(0.1f)) : IV(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);
				char rowId[40];
				std::snprintf(rowId, sizeof(rowId), "##shard%u", e.shard_id);
				ImGui::BeginChild(rowId, ImVec2(-FLT_MIN, kRowH), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(3);
				ImGui::PopStyleColor(2);

				const float innerW = ImGui::GetContentRegionAvail().x;
				const float textW = (std::max)(120.f, innerW - kFlag - 18.f - kLoadW - kPingW - 16.f);

				ImGui::BeginGroup();
				const ImVec2 flagP0 = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(kFlag, kFlag));
				const ImVec2 flagP1 = ImGui::GetItemRectMax();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(flagP0, flagP1, U32(LnTheme::kPanel), 4.f);
				dl->AddRect(flagP0, flagP1, isSelected ? U32(LnTheme::kAccent) : U32(LnTheme::kBorder), 4.f, 0, 1.5f);
				{
					const ImVec2 ts = ImGui::CalcTextSize(initialBuf);
					dl->AddText(ImVec2(flagP0.x + (kFlag - ts.x) * 0.5f, flagP0.y + (kFlag - ts.y) * 0.5f), U32(LnTheme::kText), initialBuf);
				}
				ImGui::SameLine(0.f, 14.f);
				ImGui::BeginGroup();
				ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
				ImGui::SetWindowFontScale(1.05f);
				ImGui::TextUnformatted(nameUpper.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textW);
				ImGui::TextWrapped("%s", descLine.c_str());
				ImGui::PopTextWrapPos();
				ImGui::SetWindowFontScale(0.76f);
				const std::string modeLine = tr("auth.shard_pick.mode_default");
				ImGui::TextUnformatted(modeLine.c_str());
				if (e.character_count > 0u)
				{
					const std::string ev = tr("auth.shard_pick.event_characters",
						P{{"count", std::to_string(e.character_count)}});
					ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
					ImGui::TextUnformatted(ev.c_str());
					ImGui::PopStyleColor();
				}
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::EndGroup();
				ImGui::EndGroup();

				ImGui::SameLine(0.f, 10.f);
				ImGui::BeginGroup();
				ImGui::Dummy(ImVec2(kLoadW, 1.f));
				const std::string loadLbl = tr("auth.shard_pick.load_line", P{{"percent", std::to_string(pct)}});
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.72f);
				ImGui::TextUnformatted(loadLbl.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				const LnTheme::Rgba barCol = (vis == RowVis::Offline) ? LnTheme::kMuted
					: (vis == RowVis::Saturated) ? LnTheme::kWarning : LnTheme::kSuccess;
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IV(barCol));
				ImGui::ProgressBar(loadFrac, ImVec2(kLoadW, 6.f), "");
				ImGui::PopStyleColor();
				const std::string pl = tr("auth.shard_pick.players",
					P{{"current", std::to_string(e.current_load)}, {"max", std::to_string(e.max_capacity)}});
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.78f);
				ImGui::TextUnformatted(pl.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::EndGroup();

				ImGui::SameLine(0.f, 10.f);
				ImGui::BeginGroup();
				ImGui::Dummy(ImVec2(kPingW, 1.f));
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.85f);
				ImGui::TextUnformatted(tr("auth.shard_pick.latency_na").c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				const char* statKey = (vis == RowVis::Online) ? "auth.shard_pick.status_online"
					: (vis == RowVis::Saturated) ? "auth.shard_pick.status_saturated" : "auth.shard_pick.status_offline";
				const LnTheme::Rgba stCol =
					(vis == RowVis::Online) ? LnTheme::kSuccess : (vis == RowVis::Saturated) ? LnTheme::kWarning : LnTheme::kErrorCol;
				ImGui::PushStyleColor(ImGuiCol_Text, IV(stCol));
				ImGui::SetWindowFontScale(0.78f);
				ImGui::TextUnformatted(tr(statKey).c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::EndGroup();

				ImGui::SetCursorPos(ImVec2(0.f, 0.f));
				char invId[48];
				std::snprintf(invId, sizeof(invId), "##sinv%u", e.shard_id);
				ImGui::InvisibleButton(invId, ImVec2(ImGui::GetWindowWidth() - 8.f, kRowH));
				if (ImGui::IsItemClicked() && rowSelectable && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiSetShardPickChoiceShardId(e.shard_id);
				}

				ImGui::EndChild();
				ImGui::Spacing();
				ImGui::PopStyleVar(1);
			}
		}

		ImGui::EndChild();
		ImGui::Spacing();

		const bool canEnter = (m_authPresenter != nullptr && m_authPresenter->ShardPickChoiceShardId() != 0u);
		const float backW = 148.f;
		const float enterW = 228.f;
		const std::string backStr = tr("auth.shard_pick.button_back");
		const std::string enterStr = tr("auth.shard_pick.enter_world");

		auto drawSizedGhost = [&](const char* label, float w, bool disabled) -> bool {
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
			const bool c = ImGui::Button(label, ImVec2(w, 32.f));
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			if (disabled)
			{
				ImGui::EndDisabled();
			}
			return c;
		};

		if (drawSizedGhost(backStr.c_str(), backW, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromShardPickToLogin();
		}
		ImGui::SameLine(0.f, 14.f);
		{
			const std::string navRow = tr("auth.shard_pick.hint_nav_row");
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(navRow.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}
		ImGui::SameLine(0.f, 0.f);
		ImGui::Dummy(ImVec2((std::max)(0.f, ImGui::GetContentRegionAvail().x - enterW - 4.f), 1.f));
		ImGui::SameLine(0.f, 0.f);
		if (!canEnter)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.25f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.35f)));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		const bool enterClick = ImGui::Button(enterStr.c_str(), ImVec2(enterW, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (!canEnter)
		{
			ImGui::EndDisabled();
		}
		if (enterClick && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitShardPick(*m_authCfg);
		}

		EndPanel();

		if (m_authPresenter != nullptr)
		{
			std::vector<uint32_t> selectable;
			selectable.reserve(entries.size());
			for (const auto& e : entries)
			{
				if (e.status == 1u && !e.endpoint.empty())
				{
					selectable.push_back(e.shard_id);
				}
			}
			if (!selectable.empty())
			{
				int idx = -1;
				for (size_t i = 0; i < selectable.size(); ++i)
				{
					if (selectable[i] == choice)
					{
						idx = static_cast<int>(i);
						break;
					}
				}
				if (idx < 0)
				{
					idx = 0;
				}
				const int n = static_cast<int>(selectable.size());
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
				{
					idx = (idx + 1) % n;
					m_authPresenter->ImGuiSetShardPickChoiceShardId(selectable[static_cast<size_t>(idx)]);
				}
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
				{
					idx = (idx - 1 + n) % n;
					m_authPresenter->ImGuiSetShardPickChoiceShardId(selectable[static_cast<size_t>(idx)]);
				}
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			{
				m_authPresenter->ImGuiBackFromShardPickToLogin();
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && canEnter && m_authCfg != nullptr)
			{
				m_authPresenter->ImGuiSubmitShardPick(*m_authCfg);
			}
		}

		DrawAuthTweaksPanel(vpW, vpH);
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

	void AuthImGuiRenderer::DrawBreadcrumb(const std::vector<std::string>&, int) {}
} // namespace engine::render

#endif
