#include "engine/render/AuthImGuiRenderer.h"

#include "engine/client/LocalizationService.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
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
		m_loginLangBadgeText.clear();
		m_loginLangBadgeStartTime = -1.0;
		m_prevPhaseToken = 0u;
		m_regCountryComboIdx = 0;
		m_optResIdx = 2;
		m_optQualityPreset = 2;
		m_optFovDegrees = 70.f;
		m_optUiScalePct = 100.f;
		m_optPanelOpacityPct = 70.f;
		m_optShowTooltipsUi = true;
		m_optPreferredServer = 2;
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
		m_optUiScalePct = std::clamp(m.uiScalePercent, 80.f, 140.f);
		m_optPanelOpacityPct = std::clamp(m.panelOpacityPercent, 40.f, 100.f);
		m_optShowTooltipsUi = m.showTooltipUi;
		m_optPreferredServer = static_cast<int>(m.preferredServerIndex > 2u ? 2u : m.preferredServerIndex);
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
		m_prevPhaseToken = m_lastSyncedPhaseToken;
		m_lastSyncedPhaseToken = fp;

		// Détection de la transition « écran de sélection de langue » → « écran de connexion » :
		// on capture le bandeau d'info posé par ApplyLocaleSelection (« Langue : Français » par
		// ex.) pour le re-publier au-dessus du cadre pendant quelques secondes (cf. Render()).
		// Bit 0 = languageSelection, bit 1 = login (cf. VisualFingerprint). On masque le bit 31
		// (« active ») afin de ne pas confondre l'état initial 0xffffffff avec une vraie phase.
		constexpr uint32_t kPhaseMask = 0x7FFFFFFFu;
		constexpr uint32_t kPhaseLanguageOnly = 1u << 0;
		const bool justLoggedIn = (fp & (1u << 1)) != 0u;
		const bool fromLanguageSel = (m_prevPhaseToken & kPhaseMask) == kPhaseLanguageOnly;
		if (justLoggedIn && fromLanguageSel && !rm.infoBanner.empty())
		{
			m_loginLangBadgeText = rm.infoBanner;
			m_loginLangBadgeStartTime = ImGui::GetTime();
		}

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
		if (vs.verifyEmail)
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
		if (vs.languageSelection && m_authPresenter != nullptr)
		{
			const auto& locs = m_authPresenter->GetAvailableLocales();
			if (!locs.empty())
			{
				const uint32_t maxIdx = static_cast<uint32_t>(locs.size() - 1u);
				const uint32_t idx = m_authPresenter->FirstRunLanguageSelectionIndex();
				m_selectedLang = static_cast<int>(idx > maxIdx ? maxIdx : idx);
			}
		}

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
			RenderLoginScreen(vs, rm, viewportW, viewportH);
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
		std::string_view subtitle, std::string_view versionLabel, bool versionLeadingInfoGlyph, bool subtitleWelcomeAccent,
		float fixedHeight)
	{
		const float panelX = (vpW - width) * 0.5f;
		const float panelY = vpH * 0.28f;
		ImGui::SetCursorPos(ImVec2(panelX, panelY));

		ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::PanelBg()));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f, 18.f));

		// Si fixedHeight > 0 : le panneau a une hauteur figée. Sinon AutoResizeY : la hauteur s'aligne
		// sur le contenu réel — évite les énormes panneaux vides qui poussaient les champs et boutons
		// hors de l'écran (ex. login après bannière info, choix de langue).
		const ImVec2 panelSize(width, fixedHeight > 0.f ? fixedHeight : 0.f);
		const ImGuiChildFlags childFlags = (fixedHeight > 0.f)
			? ImGuiChildFlags_Borders
			: (ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		const bool open = ImGui::BeginChild("##ln_panel", panelSize, childFlags,
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
		// Zone de clic = toute la ligne (toggle + libellé) pour que cliquer sur "SE SOUVENIR DE MOI"
		// bascule l'état comme le ferait un clic sur le toggle. Avant, l'InvisibleButton couvrait
		// uniquement la pastille (50x28 px) et le libellé n'avait aucun handler, ce qui donnait
		// l'impression que le toggle ne réagissait pas.
		std::string rememberTitle = (!rm.bodyLines.empty() && !rm.bodyLines[0].text.empty())
			? rm.bodyLines[0].text
			: std::string("SE SOUVENIR DE MOI");
		const float rowAvail = ImGui::GetContentRegionAvail().x;
		const ImVec2 hitSize(rowAvail, trackH + 6.f);
		ImGui::InvisibleButton("##remember_toggle", hitSize);
		if (ImGui::IsItemClicked())
		{
			m_rememberMe = !m_rememberMe;
		}
		// Détail (« Conserve l'identifiant à la prochaine ouverture ») désormais affiché en tooltip
		// au survol de la ligne — ne mange plus de hauteur dans le panneau.
		if (ImGui::IsItemHovered() && !rm.authRememberDetailLine.empty())
		{
			ImGui::SetTooltip("%s", rm.authRememberDetailLine.c_str());
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
		// Libellé dessiné via la draw list par-dessus la zone invisible (pas de SameLine + Text qui
		// déplacerait le curseur d'une ligne et casserait le hit-test ci-dessus).
		const float labelX = b.x + 12.f;
		const ImVec2 ts = ImGui::CalcTextSize(rememberTitle.c_str());
		const float labelY = (a.y + b.y) * 0.5f - ts.y * 0.5f;
		dl->AddText(ImVec2(labelX, labelY), U32(LnTheme::kText), rememberTitle.c_str());
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

	void AuthImGuiRenderer::DrawLoginLanguageBadge(float vpW, float vpH)
	{
		if (m_loginLangBadgeText.empty() || m_loginLangBadgeStartTime <= 0.0)
		{
			return;
		}
		const double elapsed = ImGui::GetTime() - m_loginLangBadgeStartTime;
		if (elapsed < 0.0 || elapsed >= kLoginLangBadgeDurationSec)
		{
			// Au-delà de la fenêtre d'affichage, on n'efface PAS `m_loginLangBadgeText` : il sert
			// au panneau login pour continuer à supprimer la même `infoBanner` à l'intérieur, sinon
			// le bandeau « Information / Langue appliquée immédiatement » réapparaîtrait dans le
			// cadre principal après le fade-out (effet « se déplace ») — voir RenderLoginScreen.
			m_loginLangBadgeStartTime = -1.0;
			return;
		}
		// Fade-out final (dernière `kLoginLangBadgeFadeOutSec` secondes) pour disparition douce.
		float alpha = 1.f;
		const double fadeStart = kLoginLangBadgeDurationSec - kLoginLangBadgeFadeOutSec;
		if (elapsed > fadeStart)
		{
			alpha = static_cast<float>(1.0 - (elapsed - fadeStart) / kLoginLangBadgeFadeOutSec);
			alpha = std::clamp(alpha, 0.f, 1.f);
		}

		const float panelTop = vpH * 0.28f;
		const float badgePadX = 18.f;
		const float badgePadY = 8.f;
		const ImVec2 textSz = ImGui::CalcTextSize(m_loginLangBadgeText.c_str());
		const float badgeW = textSz.x + 2.f * badgePadX;
		const float badgeH = textSz.y + 2.f * badgePadY;
		const float badgeX = (vpW - badgeW) * 0.5f;
		const float badgeY = (std::max)(8.f, panelTop - badgeH - 12.f);

		ImGui::SetNextWindowPos(ImVec2(badgeX, badgeY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(badgeW, badgeH), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(badgePadX, badgePadY));
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.92f)));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kAccent));
		ImGui::Begin("##ln_login_lang_badge",
			nullptr,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoInputs
				| ImGuiWindowFlags_NoScrollbar);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
		ImGui::TextUnformatted(m_loginLangBadgeText.c_str());
		ImGui::PopStyleColor();
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(4);
	}

	void AuthImGuiRenderer::DrawAuthTweaksPanel(float vpW, float vpH)
	{
		static constexpr const char* kRaceLabels[] = {"DEFAUT", "HUMAINS", "ELFES", "NAINS", "ORCS", "MORTS-V.", "CORROM.",
			"DIVINS", "DEMONS"};
		const float winW = 272.f;
		const float winH = 218.f;

		// Le titre « TWEAKS » et son bouton de réduction (- / +) ont été retirés à la demande
		// de l'utilisateur : le panneau est désormais toujours affiché expansé, sans header.
		// `m_authTweakPanelMinimized` reste comme placeholder mais n'est plus relu.

		ImGui::SetNextWindowPos(ImVec2(vpW - winW - 22.f, vpH - (winH + 10.f)), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 12.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.78f)));
		ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
		ImGui::Begin("##ln_auth_tweaks",
			nullptr,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus);

		// Le panneau Tweaks doit utiliser une typographie plus discrète que le cadre principal :
		// 0.85x compense le titre login agrandi, en gardant les boutons cliquables.
		ImGui::SetWindowFontScale(0.85f);

		// Sans le titre « TWEAKS », le contenu se retrouvait collé en haut avec un grand vide
		// au bas du cadre. On centre verticalement en injectant un Dummy au-dessus de
		// « THEME DE RACE » dimensionné pour pousser le contenu de quelques pixels vers le bas.
		// Heuristique : le contenu (label race + grille 3x3 + label fond + paire de boutons)
		// fait environ 165 px à 0.85x. Avec windowPadding 12 + 12 = 24, total = 189 px de
		// contenu sur 218 px de fenêtre → 29 px vides répartis. On en met 18 en haut pour que
		// le contenu visuel paraisse centré (avec un léger biais vers le bas pour l'esthétique).
		ImGui::Dummy(ImVec2(0.f, 18.f));

		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
		ImGui::TextUnformatted("THEME DE RACE");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		// Boutons « race » : la sélection courante doit ressortir visuellement (texte ET bordure
		// en accent), sinon l'utilisateur ne distingue pas l'état actif. PushStyleColor(Text)
		// par bouton pour ne pas écraser l'état des autres.
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
				ImGui::PushStyleColor(ImGuiCol_Text, sel ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, sel ? 1.5f : 1.f);
				char id[40];
				std::snprintf(id, sizeof(id), "%s##race_%d", kRaceLabels[idx], idx);
				if (ImGui::Button(id, ImVec2(btnW, 0.f)))
				{
					m_langTweakRace = idx;
				}
				ImGui::PopStyleVar(1);
				ImGui::PopStyleColor(2);
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
			// Le toggle ACTIVE / DESACTIVE pilote le futur fond animé de l'écran d'auth.
			// Tant que l'animation n'est pas branchée côté Vulkan (passe de fond), seul le
			// rendu visuel des deux boutons reflète l'état choisi. Voir CODEBASE_MAP.md §13.
			const float half = (ImGui::GetContentRegionAvail().x - 6.f) * 0.5f;
			const bool on = m_langTweakAnimBg;
			ImGui::PushStyleColor(ImGuiCol_Border, on ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, on ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, on ? 1.5f : 1.f);
			if (ImGui::Button("ACTIVE##lang_bg_on", ImVec2(half, 0.f)))
			{
				m_langTweakAnimBg = true;
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(2);
			ImGui::SameLine(0.f, 6.f);
			const bool off = !m_langTweakAnimBg;
			ImGui::PushStyleColor(ImGuiCol_Border, off ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, off ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, off ? 1.5f : 1.f);
			if (ImGui::Button("DESACTIVE##lang_bg_off", ImVec2(half, 0.f)))
			{
				m_langTweakAnimBg = false;
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(2);
		}
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(3);

		ImGui::SetWindowFontScale(1.f);
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
		std::string bannerId = "##banner_";
		bannerId.append(title.data(), title.size());
		// AutoResizeY : la bannière s'adapte à la hauteur réelle du texte au lieu de remplir tout le panneau.
		ImGui::BeginChild(bannerId.c_str(), ImVec2(-FLT_MIN, 0.f),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
			ImGuiWindowFlags_NoScrollbar);
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

	bool AuthImGuiRenderer::DrawPrimaryButton(std::string_view label, bool disabled, float width)
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
		const bool clicked = ImGui::Button(id, ImVec2(width, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (disabled)
		{
			ImGui::EndDisabled();
		}
		return clicked;
	}

	bool AuthImGuiRenderer::DrawGhostButton(std::string_view label, bool disabled, float width)
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
		const bool clicked = ImGui::Button(id, ImVec2(width, 32.f));
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
