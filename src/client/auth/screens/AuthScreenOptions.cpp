// AUTH-UI.6 — Couche modèle pour l'overlay Options (phase LanguageOptions).

// Couche modèle : BuildModel_Options expose les catégories et leurs réglages, Update_Options gère la navigation clavier.
#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace engine::client
{
#if defined(_WIN32)
	namespace
	{
		/// Arrondit un volume au dixième le plus proche (0.0–1.0) pour éviter les flottants imprévisibles dans l'affichage.
		float ClampOptionVolume(float value)
		{
			const float clamped = std::clamp(value, 0.0f, 1.0f);
			const int scaled = static_cast<int>(clamped * 10.0f + 0.5f);
			return static_cast<float>(scaled) / 10.0f;
		}
	} // namespace

	/// Ouvre l'overlay Options depuis le renderer ImGui (pont vers OpenLanguageOptions).
	void AuthUiPresenter::ImGuiOpenLanguageOptionsMenu()
	{
		OpenLanguageOptions();
	}

	/// Exécute une action du sous-menu Compte (changer mot de passe, e-mail, ou déconnexion).
	void AuthUiPresenter::ImGuiOptionsAccountMenuAction(OptionsAccountMenuAction action)
	{
		if (m_phase != Phase::LanguageOptions)
		{
			return;
		}
		switch (action)
		{
		case OptionsAccountMenuAction::ChangePassword:
			ImGuiCloseLanguageOptionsWithoutApply();
			if (m_phase == Phase::Login)
			{
				ImGuiNavigateToForgotFromLogin();
			}
			else
			{
				m_infoBanner = Tr("options.account.password_need_login");
			}
			break;
		case OptionsAccountMenuAction::ChangeEmail:
			ImGuiCloseLanguageOptionsWithoutApply();
			m_infoBanner = Tr("options.account.email_hint");
			break;
		case OptionsAccountMenuAction::SignOut:
			ImGuiCloseLanguageOptionsWithoutApply();
			m_password.clear();
			m_passwordConfirm.clear();
			m_registeredTagId.clear();
			m_infoBanner = Tr("options.account.signed_out");
			break;
		}
	}

	/// Peuple le modèle du menu Options : racine (liste catégories) ou sous-menu actif (langue, vidéo, audio, contrôles, jeu).
	void AuthUiPresenter::BuildModel_Options(RenderModel& model) const
	{
		auto addOptionsRow = [this, &model](std::string label, std::string value, bool active)
		{
			RenderBodyLine row{};
			row.text = std::move(label);
			row.valueText = std::move(value);
			row.active = active;
			row.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
			model.bodyLines.push_back(std::move(row));
		};

		auto addActionKeys = [this, &model](std::string_view labelKey, bool primary, bool active = true, bool emphasized = false,
			std::string_view labelKeyFallback = {})
		{
			RenderAction a{};
			a.labelKey = std::string(labelKey);
			if (!labelKeyFallback.empty())
			{
				a.labelKeyFallback = std::string(labelKeyFallback);
			}
			a.primary = primary;
			a.active = active;
			a.emphasized = emphasized;
			a.hovered = static_cast<int32_t>(model.actions.size()) == m_hoveredActionIndex;
			model.actions.push_back(std::move(a));
		};

		model.authOptionsAccountLogin = m_login;
		model.authOptionsAccountTagId = m_registeredTagId;

		const auto& locales = m_localization.GetAvailableLocales();
		if (m_optionsSubMenu == OptionsSubMenu::Root)
		{
			model.sectionTitle = Tr("language.options.title");
			{
				RenderBodyLine line{};
				line.text = Tr("language.current", { { "language", LocalizedLanguageName(m_selectedLocale) } });
				line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
				model.bodyLines.push_back(std::move(line));
			}
			addOptionsRow(Tr("options.menu.language"), Tr("options.menu.chevron"), m_optionsRootSelection == 0u);
			addOptionsRow(Tr("options.menu.video"), Tr("options.menu.chevron"), m_optionsRootSelection == 1u);
			addOptionsRow(Tr("options.menu.audio"), Tr("options.menu.chevron"), m_optionsRootSelection == 2u);
			addOptionsRow(Tr("options.menu.controls"), Tr("options.menu.chevron"), m_optionsRootSelection == 3u);
			addOptionsRow(Tr("options.menu.game"), Tr("options.menu.chevron"), m_optionsRootSelection == 4u);
			model.footerHint = Tr("options.menu.hint_root");
		}
		else
		{
			std::string subLabel;
			switch (m_optionsSubMenu)
			{
			case OptionsSubMenu::Language:
				subLabel = Tr("options.menu.language");
				break;
			case OptionsSubMenu::Video:
				subLabel = Tr("options.menu.video");
				break;
			case OptionsSubMenu::Audio:
				subLabel = Tr("options.menu.audio");
				break;
			case OptionsSubMenu::Controls:
				subLabel = Tr("options.menu.controls");
				break;
			case OptionsSubMenu::Game:
				subLabel = Tr("options.menu.game");
				break;
			case OptionsSubMenu::Root:
				subLabel.clear();
				break;
			}
			model.sectionTitle = Tr("language.options.title") + " - " + subLabel;
			model.footerHint = Tr("options.menu.hint_submenu");

			switch (m_optionsSubMenu)
			{
			case OptionsSubMenu::Language:
			{
				{
					RenderBodyLine line{};
					line.text = Tr("language.current", { { "language", LocalizedLanguageName(m_selectedLocale) } });
					line.active = m_optionsSubSelection == 0u;
					line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
					model.bodyLines.push_back(std::move(line));
				}
				if (!locales.empty())
				{
					const std::string& selectedLocale = locales[m_languageSelectionIndex % static_cast<uint32_t>(locales.size())];
					RenderBodyLine line{};
					line.text = "< " + LocalizedLanguageName(selectedLocale) + " (" + selectedLocale + ") >";
					line.active = m_optionsSubSelection == 1u;
					line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
					model.bodyLines.push_back(std::move(line));
				}
				else
				{
					RenderBodyLine line{};
					line.text = "< N/A >";
					line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
					model.bodyLines.push_back(std::move(line));
				}
				break;
			}
			case OptionsSubMenu::Video:
				addOptionsRow(Tr("options.video.fullscreen"), Tr(m_videoFullscreenPending ? "options.value.on" : "options.value.off"),
					m_optionsSubSelection == 0u);
				addOptionsRow(Tr("options.video.vsync"), Tr(m_videoVsyncPending ? "options.value.on" : "options.value.off"),
					m_optionsSubSelection == 1u);
				break;
			case OptionsSubMenu::Audio:
				addOptionsRow(Tr("options.audio.master"), std::to_string(static_cast<int>(m_audioMasterVolumePending * 100.0f + 0.5f)) + "%",
					m_optionsSubSelection == 0u);
				addOptionsRow(Tr("options.audio.music"), std::to_string(static_cast<int>(m_audioMusicVolumePending * 100.0f + 0.5f)) + "%",
					m_optionsSubSelection == 1u);
				addOptionsRow(Tr("options.audio.sfx"), std::to_string(static_cast<int>(m_audioSfxVolumePending * 100.0f + 0.5f)) + "%",
					m_optionsSubSelection == 2u);
				addOptionsRow(Tr("options.audio.ui"), std::to_string(static_cast<int>(m_audioUiVolumePending * 100.0f + 0.5f)) + "%",
					m_optionsSubSelection == 3u);
				break;
			case OptionsSubMenu::Controls:
				addOptionsRow(Tr("options.controls.mouse_sensitivity"),
					std::to_string(static_cast<int>(m_mouseSensitivityPending * 10000.0f + 0.5f)), m_optionsSubSelection == 0u);
				addOptionsRow(Tr("options.controls.invert_y"), Tr(m_invertYPending ? "options.value.on" : "options.value.off"),
					m_optionsSubSelection == 1u);
				addOptionsRow(Tr("options.controls.movement_layout"),
					Tr(m_useZqsdPending ? "options.controls.layout.zqsd" : "options.controls.layout.wasd"), m_optionsSubSelection == 2u);
				break;
			case OptionsSubMenu::Game:
				addOptionsRow(Tr("options.game.gameplay_udp"), Tr(m_gameplayUdpEnabledPending ? "options.value.on" : "options.value.off"),
					m_optionsSubSelection == 0u);
				addOptionsRow(Tr("options.game.allow_insecure_dev"), Tr(m_allowInsecureDevPending ? "options.value.on" : "options.value.off"),
					m_optionsSubSelection == 1u);
				addOptionsRow(Tr("options.game.auth_timeout"), std::to_string(m_authTimeoutMsPending) + " ms", m_optionsSubSelection == 2u);
				break;
			case OptionsSubMenu::Root:
				break;
			}
		}

		addActionKeys("language.options.apply_hint", true, true, false, "common.submit");
		addActionKeys("auth.hint.return_login", false, true, false, "common.back");
	}

	/// Gère la navigation clavier dans le menu Options (flèches, Entrée, Échap) hors ImGui.
	void AuthUiPresenter::Update_Options(engine::platform::Input& input, const engine::core::Config&, engine::platform::Window&,
		bool usingNativeAuth, bool authUiImguiMode)
	{
		if (usingNativeAuth || authUiImguiMode || m_phase != Phase::LanguageOptions)
		{
			return;
		}

		if (m_viewportW > 0u && m_viewportH > 0u && m_initialized && !m_flowComplete && m_authEnabled && m_optionsSubMenu == OptionsSubMenu::Root
			&& input.MouseScrollDelta() != 0)
		{
			const int d = input.MouseScrollDelta() > 0 ? -1 : 1;
			const uint32_t kRootCategoryCount = 5u;
			if (d < 0)
			{
				m_optionsRootSelection = (m_optionsRootSelection == 0u) ? (kRootCategoryCount - 1u) : (m_optionsRootSelection - 1u);
			}
			else
			{
				m_optionsRootSelection = (m_optionsRootSelection + 1u) % kRootCategoryCount;
			}
		}

		const auto& locales = m_localization.GetAvailableLocales();
		const uint32_t kRootCategoryCount = 5u;
		if (m_optionsSubMenu == OptionsSubMenu::Root)
		{
			if (input.WasPressed(engine::platform::Key::Up))
			{
				m_optionsRootSelection = (m_optionsRootSelection == 0u) ? (kRootCategoryCount - 1u) : (m_optionsRootSelection - 1u);
				LOG_INFO(Core, "[AuthUiPresenter] Options root selection={}", m_optionsRootSelection);
			}
			if (input.WasPressed(engine::platform::Key::Down))
			{
				m_optionsRootSelection = (m_optionsRootSelection + 1u) % kRootCategoryCount;
				LOG_INFO(Core, "[AuthUiPresenter] Options root selection={}", m_optionsRootSelection);
			}
		}
		else
		{
			const uint32_t n = OptionsSubmenuLineCount(m_optionsSubMenu);
			if (n > 0u)
			{
				if (input.WasPressed(engine::platform::Key::Up))
				{
					m_optionsSubSelection = (m_optionsSubSelection == 0u) ? (n - 1u) : (m_optionsSubSelection - 1u);
					LOG_INFO(Core, "[AuthUiPresenter] Options sub selection={}", m_optionsSubSelection);
				}
				if (input.WasPressed(engine::platform::Key::Down))
				{
					m_optionsSubSelection = (m_optionsSubSelection + 1u) % n;
					LOG_INFO(Core, "[AuthUiPresenter] Options sub selection={}", m_optionsSubSelection);
				}
			}

			if (!locales.empty() && m_optionsSubMenu == OptionsSubMenu::Language
				&& (m_optionsSubSelection == 0u || m_optionsSubSelection == 1u)
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				if (input.WasPressed(engine::platform::Key::Left))
				{
					m_languageSelectionIndex =
						(m_languageSelectionIndex == 0u) ? static_cast<uint32_t>(locales.size() - 1u) : (m_languageSelectionIndex - 1u);
				}
				else
				{
					m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % static_cast<uint32_t>(locales.size());
				}
				m_selectedLocale = locales[m_languageSelectionIndex];
				LOG_INFO(Core, "[AuthUiPresenter] Options locale candidate={}", m_selectedLocale);
			}
			if (m_optionsSubMenu == OptionsSubMenu::Video && m_optionsSubSelection == 0u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				m_videoFullscreenPending = !m_videoFullscreenPending;
				LOG_INFO(Core, "[AuthUiPresenter] Options fullscreen candidate={}", m_videoFullscreenPending);
			}
			if (m_optionsSubMenu == OptionsSubMenu::Video && m_optionsSubSelection == 1u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				m_videoVsyncPending = !m_videoVsyncPending;
				LOG_INFO(Core, "[AuthUiPresenter] Options vsync candidate={}", m_videoVsyncPending);
			}
			auto adjustVolume = [&](float& value, std::string_view label)
			{
				if (input.WasPressed(engine::platform::Key::Left))
				{
					value = ClampOptionVolume(value - 0.1f);
				}
				else
				{
					value = ClampOptionVolume(value + 0.1f);
				}
				LOG_INFO(Core, "[AuthUiPresenter] Options {} candidate={:.1f}", label, value);
			};
			if (m_optionsSubMenu == OptionsSubMenu::Audio && m_optionsSubSelection == 0u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				adjustVolume(m_audioMasterVolumePending, "master");
			}
			if (m_optionsSubMenu == OptionsSubMenu::Audio && m_optionsSubSelection == 1u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				adjustVolume(m_audioMusicVolumePending, "music");
			}
			if (m_optionsSubMenu == OptionsSubMenu::Audio && m_optionsSubSelection == 2u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				adjustVolume(m_audioSfxVolumePending, "sfx");
			}
			if (m_optionsSubMenu == OptionsSubMenu::Audio && m_optionsSubSelection == 3u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				adjustVolume(m_audioUiVolumePending, "ui");
			}
			if (m_optionsSubMenu == OptionsSubMenu::Controls && m_optionsSubSelection == 0u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				if (input.WasPressed(engine::platform::Key::Left))
				{
					m_mouseSensitivityPending = std::max(0.001f, m_mouseSensitivityPending - 0.001f);
				}
				else
				{
					m_mouseSensitivityPending = std::min(0.010f, m_mouseSensitivityPending + 0.001f);
				}
				LOG_INFO(Core, "[AuthUiPresenter] Options mouse sensitivity candidate={:.4f}", m_mouseSensitivityPending);
			}
			if (m_optionsSubMenu == OptionsSubMenu::Controls && m_optionsSubSelection == 1u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				m_invertYPending = !m_invertYPending;
				LOG_INFO(Core, "[AuthUiPresenter] Options invert_y candidate={}", m_invertYPending);
			}
			if (m_optionsSubMenu == OptionsSubMenu::Controls && m_optionsSubSelection == 2u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				m_useZqsdPending = !m_useZqsdPending;
				LOG_INFO(Core, "[AuthUiPresenter] Options movement layout candidate={}", m_useZqsdPending ? "zqsd" : "wasd");
			}
			if (m_optionsSubMenu == OptionsSubMenu::Game && m_optionsSubSelection == 0u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				m_gameplayUdpEnabledPending = !m_gameplayUdpEnabledPending;
				LOG_INFO(Core, "[AuthUiPresenter] Options gameplay_udp candidate={}", m_gameplayUdpEnabledPending);
			}
			if (m_optionsSubMenu == OptionsSubMenu::Game && m_optionsSubSelection == 1u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				m_allowInsecureDevPending = !m_allowInsecureDevPending;
				LOG_INFO(Core, "[AuthUiPresenter] Options allow_insecure_dev candidate={}", m_allowInsecureDevPending);
			}
			if (m_optionsSubMenu == OptionsSubMenu::Game && m_optionsSubSelection == 2u
				&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
			{
				if (input.WasPressed(engine::platform::Key::Left))
				{
					m_authTimeoutMsPending = (m_authTimeoutMsPending > 1000u) ? (m_authTimeoutMsPending - 1000u) : 1000u;
				}
				else
				{
					m_authTimeoutMsPending = std::min<uint32_t>(15000u, m_authTimeoutMsPending + 1000u);
				}
				LOG_INFO(Core, "[AuthUiPresenter] Options auth timeout candidate={}ms", m_authTimeoutMsPending);
			}
		}
	}

// Stubs Linux/Mac — aucune UI d'auth sur ces plateformes.
#else

	void AuthUiPresenter::ImGuiOpenLanguageOptionsMenu() {}

	void AuthUiPresenter::ImGuiOptionsAccountMenuAction(OptionsAccountMenuAction) {}

	void AuthUiPresenter::BuildModel_Options(RenderModel&) const {}

	void AuthUiPresenter::Update_Options(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool) {}

#endif
} // namespace engine::client
