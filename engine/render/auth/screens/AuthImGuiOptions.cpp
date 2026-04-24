// AUTH-UI.6 — overlay Options de la phase LanguageOptions : sidebar de navigation par onglets et panneau principal multi-sections (split depuis AuthImGuiRenderer.cpp).
// Contient RenderOptionsScreen avec ses lambdas internes (sliderVol01, sectionTitle, hintLine, toggleRow, submitOptionsMirror) et les sept onglets de configuration.

#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/auth/AuthImGuiCommon.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

		/// Convertit une couleur LnTheme::Rgba en ImU32 pour les appels de draw list ImGui.
		ImU32 U32(const LnTheme::Rgba& c)
		{
			return ImGui::ColorConvertFloat4ToU32(IV(c));
		}

		constexpr int kOptionsRes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}}; ///< Table des résolutions vidéo proposées dans le combo graphique.
		constexpr int kOptionsResCount = sizeof(kOptionsRes) / sizeof(kOptionsRes[0]); ///< Nombre d'entrées dans kOptionsRes, calculé statiquement.
	} // namespace

	/// Affiche l'overlay Options complet : sidebar de navigation par onglets à gauche, panneau principal à droite avec les réglages de l'onglet actif, et barre de boutons Retour / Annuler / Appliquer en bas.
	void AuthImGuiRenderer::RenderOptionsScreen(const RenderModel& rm, float vpW, float vpH)
	{
		const auto tr = [this](const char* key, const char* fallback = nullptr) -> std::string {
			if (m_authPresenter == nullptr)
			{
				return fallback && fallback[0] != '\0' ? std::string(fallback) : std::string(key);
			}
			std::string s = m_authPresenter->UiTranslate(key);
			if (s.empty() && fallback != nullptr && fallback[0] != '\0')
			{
				return std::string(fallback);
			}
			return s.empty() ? std::string(key) : s;
		};

		const float sideW = 220.f;
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
			if (active)
			{
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 rmin = ImGui::GetItemRectMin();
				const ImVec2 rmax = ImGui::GetItemRectMax();
				dl->AddRectFilled(ImVec2(rmin.x, rmin.y), ImVec2(rmin.x + 3.f, rmax.y), U32(LnTheme::kAccent));
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
			mir.authTimeoutMs = std::clamp(m_optAuthTimeoutMs, 2000u, 10000u);
			mir.languageSelectionIndex = static_cast<uint32_t>(m_optLangIndex);
			mir.uiScalePercent = std::clamp(m_optUiScalePct, 80.f, 140.f);
			mir.panelOpacityPercent = std::clamp(m_optPanelOpacityPct, 40.f, 100.f);
			mir.showTooltipUi = m_optShowTooltipsUi;
			mir.preferredServerIndex = static_cast<uint32_t>(std::clamp(m_optPreferredServer, 0, 2));
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
			sliderVol01(tr("options.audio.voice").c_str(), &m_optAudioUi);
		}
		else if (m_optionsTab == 2)
		{
			sectionTitle("options.imgui.section.mouse_keyboard");
			float sensPct = (m_optMouseSens - 0.001f) / 0.009f * 90.f + 10.f;
			sensPct = std::clamp(sensPct, 10.f, 100.f);
			if (ImGui::SliderFloat(tr("options.controls.mouse_sensitivity").c_str(), &sensPct, 10.f, 100.f, "%.0f %%"))
			{
				m_optMouseSens = 0.001f + (sensPct - 10.f) / 90.f * 0.009f;
				m_optMouseSens = std::clamp(m_optMouseSens, 0.001f, 0.01f);
				markDirty();
			}
			hintLine("options.imgui.hint.mouse_sensitivity_pct");
			toggleRow("options.controls.invert_y", &m_optInvertY, "options.imgui.hint.invert_y");
			toggleRow("options.imgui.zqsd_layout", &m_optUseZqsd, "options.imgui.hint.zqsd");

			sectionTitle("options.imgui.section.keybinds");
			DrawAuthKeybind(tr("options.keybind.forward", "Avancer"), "Z / W");
			DrawAuthKeybind(tr("options.keybind.strafe_left", "Gauche"), "Q / A");
			DrawAuthKeybind(tr("options.keybind.backward", "Reculer"), "S");
			DrawAuthKeybind(tr("options.keybind.strafe_right", "Droite"), "D");
			DrawAuthKeybind(tr("options.keybind.interact", "Interagir"), "E");
			DrawAuthKeybind(tr("options.keybind.spell1", "Sort 1"), "1");
			DrawAuthKeybind(tr("options.keybind.spell2", "Sort 2"), "2");
			DrawAuthKeybind(tr("options.keybind.inventory", "Inventaire"), "I");
			DrawAuthKeybind(tr("options.keybind.map", "Carte"), "M");
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
					hintLine("options.imgui.lang_restart_hint");
				}
			}
			else
			{
				ImGui::TextDisabled("(Presenter)");
			}
		}
		else if (m_optionsTab == 4)
		{
			sectionTitle("options.imgui.section.interface");
			if (ImGui::SliderFloat(tr("options.interface.scale").c_str(), &m_optUiScalePct, 80.f, 140.f, "%.0f %%"))
			{
				m_optUiScalePct = std::clamp(m_optUiScalePct, 80.f, 140.f);
				markDirty();
			}
			if (ImGui::SliderFloat(tr("options.interface.panel_opacity").c_str(), &m_optPanelOpacityPct, 40.f, 100.f, "%.0f %%"))
			{
				m_optPanelOpacityPct = std::clamp(m_optPanelOpacityPct, 40.f, 100.f);
				markDirty();
			}
			toggleRow("options.interface.tooltips", &m_optShowTooltipsUi, "options.interface.tooltips_hint");
		}
		else if (m_optionsTab == 5)
		{
			sectionTitle("options.imgui.section.network");
			{
				const std::string srvLab = tr("options.imgui.pref_server");
				std::string srvItems;
				srvItems += tr("options.imgui.server_morne");
				srvItems.push_back('\0');
				srvItems += tr("options.imgui.server_korvath");
				srvItems.push_back('\0');
				srvItems += tr("options.imgui.server_auto");
				srvItems.push_back('\0');
				srvItems.push_back('\0');
				if (ImGui::Combo(srvLab.c_str(), &m_optPreferredServer, srvItems.c_str()))
				{
					m_optPreferredServer = std::clamp(m_optPreferredServer, 0, 2);
					markDirty();
				}
			}
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
			ImGui::TextUnformatted(tr("options.imgui.latency_label").c_str());
			ImGui::SameLine(0.f, 12.f);
			if (m_authPresenter != nullptr)
			{
				const auto& sc = m_authPresenter->GetStatusCache();
				ImGui::PushStyleColor(ImGuiCol_Text, sc.authOk ? IV(LnTheme::kSuccess) : IV(LnTheme::kMuted));
				const std::string lat = sc.authOk ? tr("options.imgui.latency_ok") : std::string("--");
				ImGui::TextUnformatted(lat.c_str());
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::TextUnformatted("--");
			}
			ImGui::PopStyleColor();
			ImGui::Spacing();
			toggleRow("options.game.gameplay_udp", &m_optGameplayUdp, "options.imgui.hint.gameplay_udp");
			toggleRow("options.game.allow_insecure_dev", &m_optAllowInsecureDev, "options.imgui.hint.allow_insecure");
			int tmo = static_cast<int>(m_optAuthTimeoutMs);
			if (ImGui::SliderInt(tr("options.game.auth_timeout").c_str(), &tmo, 2000, 10000, "%d ms"))
			{
				m_optAuthTimeoutMs = static_cast<uint32_t>(std::clamp(tmo, 2000, 10000));
				markDirty();
			}
		}
		else if (m_optionsTab == 6)
		{
			sectionTitle("options.imgui.account.section");
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IV(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
			ImGui::BeginChild("##acct_card", ImVec2(-FLT_MIN, 92.f), true, ImGuiWindowFlags_None);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			const std::string& loginDisp = rm.authOptionsAccountLogin.empty() ? tr("options.imgui.account_anon") : rm.authOptionsAccountLogin;
			ImGui::TextUnformatted(loginDisp.c_str());
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			const std::string& tagDisp = rm.authOptionsAccountTagId.empty() ? std::string("—") : rm.authOptionsAccountTagId;
			ImGui::TextUnformatted(tagDisp.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();
			ImGui::Spacing();
			sectionTitle("options.imgui.account.actions");
			{
				const std::string lblPw = tr("options.imgui.account_change_password");
				if (DrawAuthButtonGhost(lblPw, "##acct_pw") && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiOptionsAccountMenuAction(engine::client::AuthUiPresenter::OptionsAccountMenuAction::ChangePassword);
				}
			}
			{
				const std::string lblMail = tr("options.imgui.account_change_email");
				if (DrawAuthButtonGhost(lblMail, "##acct_mail") && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiOptionsAccountMenuAction(engine::client::AuthUiPresenter::OptionsAccountMenuAction::ChangeEmail);
				}
			}
			{
				const std::string lblOut = tr("options.imgui.account_sign_out");
				if (DrawAuthButtonDanger(lblOut, "##acct_out") && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiOptionsAccountMenuAction(engine::client::AuthUiPresenter::OptionsAccountMenuAction::SignOut);
				}
			}
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
} // namespace engine::render

#endif
