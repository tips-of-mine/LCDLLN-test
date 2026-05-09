// AUTH-UI.1 — Écran de connexion : saisie identifiant/mot de passe, navigation vers inscription/récupération.
//
// Couche modèle : BuildModel_* peuple RenderModel, Update_* gère les entrées clavier hors ImGui, ImGui* reçoit les actions du renderer.
#include "src/client/auth/AuthUi.h"
#include "src/client/render/AuthUiRenderer.h"
#include "src/shared/core/DefaultClientEndpoints.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/platform/Input.h"
#include "src/shared/platform/Window.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace engine::client
{
#if defined(_WIN32)
namespace
{
	constexpr std::string_view kProductionWebPortalResetUrl =
		"https://lcdlln-portal.tips-of-mine.com/password-recovery";

	std::string ResolvePasswordRecoveryUrl(const engine::core::Config& cfg)
	{
		const std::string fromCfg = cfg.GetString("client.web_portal_reset_url", "");
		if (!fromCfg.empty())
		{
			return fromCfg;
		}
		return std::string(kProductionWebPortalResetUrl);
	}
} // namespace

	/// Peuple le RenderModel avec les champs, actions et raccourcis clavier de l'écran de connexion.
	void AuthUiPresenter::BuildModel_Login(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.section.login");
		model.authLoginVersionBadge = Tr("auth.login.version_badge");
		model.authRememberDetailLine = Tr("auth.login.remember_detail");

		std::string maskedPw;
		AppendPasswordStars(maskedPw, m_password.size());

		{
			RenderField f{};
			f.label = Tr("auth.label.login");
			f.value = m_login;
			f.active = m_activeField == 0u;
			f.hovered = m_hoveredFieldIndex == 0;
			f.secret = false;
			f.cyclePicker = false;
			f.tooltipKey = "auth.tooltip.login";
			f.inputPlaceholder = Tr("auth.placeholder.login");
			model.fields.push_back(std::move(f));
		}
		{
			RenderField f{};
			f.label = Tr("auth.label.password");
			f.value = maskedPw;
			f.active = m_activeField == 1u;
			f.hovered = m_hoveredFieldIndex == 1;
			f.secret = true;
			f.cyclePicker = false;
			f.tooltipKey = "auth.tooltip.password";
			f.inputPlaceholder = Tr("auth.placeholder.password");
			model.fields.push_back(std::move(f));
		}

		{
			RenderBodyLine line{};
			line.text = Tr("auth.login.remember_title");
			line.checkbox = true;
			line.checkboxChecked = m_rememberLogin;
			line.hovered = m_hoveredBodyLineIndex == 0;
			model.bodyLines.push_back(std::move(line));
		}
		{
			RenderBodyLine line{};
			line.text = Tr("auth.button.forgot_password");
			line.link = true;
			line.hovered = m_hoveredBodyLineIndex == 1;
			model.bodyLines.push_back(std::move(line));
		}

		model.authLoginFooterChips = {
			{ Tr("auth.footer.chip.tab.key"), Tr("auth.footer.chip.tab.desc") },
			{ Tr("auth.footer.chip.enter.key"), Tr("auth.footer.chip.enter.desc") },
			{ Tr("auth.footer.chip.esc.key"), Tr("auth.footer.chip.esc.desc") },
		};

		{
			RenderAction a{};
			a.labelKey = "auth.login.maquette_create";
			a.labelKeyFallback = "auth.button.register";
			a.primary = false;
			a.active = true;
			a.emphasized = false;
			a.hovered = m_hoveredActionIndex == 0;
			model.actions.push_back(std::move(a));
		}
		{
			RenderAction a{};
			a.labelKey = "auth.login.footer_options";
			a.primary = false;
			a.active = true;
			a.emphasized = false;
			a.hovered = m_hoveredActionIndex == 1;
			model.actions.push_back(std::move(a));
		}
		{
			RenderAction a{};
			a.labelKey = "auth.login.maquette_submit";
			a.labelKeyFallback = "common.submit";
			a.primary = true;
			a.active = true;
			a.emphasized = true;
			a.hovered = m_hoveredActionIndex == 2;
			model.actions.push_back(std::move(a));
		}
		{
			RenderAction a{};
			a.labelKey = "common.quit_desktop";
			a.labelKeyFallback = "common.quit";
			a.primary = false;
			a.active = true;
			a.emphasized = false;
			a.hovered = m_hoveredActionIndex == 3;
			model.actions.push_back(std::move(a));
		}
	}

	/// Traite les raccourcis clavier globaux de l'écran de connexion (Ctrl+R, Ctrl+F, Ctrl+O).
	void AuthUiPresenter::Update_LoginShortcuts(engine::platform::Input& input, const engine::core::Config& cfg,
		engine::platform::Window& window, bool usingNativeAuth, bool authUiImguiMode)
	{
		(void)authUiImguiMode;
		if (usingNativeAuth || m_phase != Phase::Login)
		{
			return;
		}
		if (!input.IsDown(engine::platform::Key::Control))
		{
			return;
		}
		if (input.WasPressed(engine::platform::Key::R))
		{
			ImGuiNavigateToRegisterFromLogin();
		}
		if (input.WasPressed(engine::platform::Key::F))
		{
			ImGuiOpenForgotPasswordPortal(cfg, window);
		}
		if (input.WasPressed(engine::platform::Key::O))
		{
			OpenLanguageOptions();
		}
	}

	/// Reçoit les identifiants saisis dans ImGui et déclenche la soumission du formulaire de connexion.
	void AuthUiPresenter::ImGuiSubmitLogin(const engine::core::Config& cfg, const char* loginUtf8, const char* passwordUtf8,
		bool rememberMe)
	{
		if (m_phase != Phase::Login)
		{
			return;
		}
		m_login = loginUtf8 ? loginUtf8 : "";
		m_password = passwordUtf8 ? passwordUtf8 : "";
		m_rememberLogin = rememberMe;
		SubmitCurrentPhase(cfg);
	}

	/// Bascule vers l'écran d'inscription depuis la connexion, en réinitialisant l'état du formulaire.
	void AuthUiPresenter::ImGuiNavigateToRegisterFromLogin()
	{
		if (m_phase != Phase::Login)
		{
			return;
		}
		SetPhase(Phase::Register);
		m_activeField = 0;
		m_userErrorText.clear();
		m_passwordConfirm.clear();
		m_usernameCheckState = UsernameCheckState::Idle;
		m_usernameCheckSeq++;
		m_usernameDebounceTimer = 0.0;
		m_usernameLastChecked.clear();
	}

	/// Retourne à l'écran de connexion depuis l'inscription en nettoyant l'état du formulaire d'inscription.
	void AuthUiPresenter::ImGuiBackFromRegisterToLogin()
	{
		if (m_phase != Phase::Register)
		{
			return;
		}
		m_usernameCheckState = UsernameCheckState::Idle;
		m_usernameCheckSeq++;
		m_usernameDebounceTimer = 0.0;
		m_usernameLastChecked.clear();
		SetPhase(Phase::Login);
		m_activeField = 0;
		m_userErrorText.clear();
		m_passwordConfirm.clear();
		m_registeredTagId.clear();
		// L'inscription en cours est annulée : la prochaine connexion ne doit pas court-circuiter ShardPick → MasterFlow.
		m_postRegistrationCharacterCreatePending = false;
	}

	/// Ouvre le portail web de récupération de mot de passe dans le navigateur système.
	void AuthUiPresenter::ImGuiOpenForgotPasswordPortal(const engine::core::Config& cfg, engine::platform::Window& window)
	{
		if (m_phase != Phase::Login)
		{
			return;
		}
		const std::string resetUrl = ResolvePasswordRecoveryUrl(cfg);
		LOG_INFO(Core, "[AuthUiPresenter] ImGui: ouverture portail recuperation ({})", resetUrl);
		if (!window.OpenExternalUrl(resetUrl))
		{
			EnterAuthErrorPhase(Phase::Login, Tr("auth.error.open_recovery_portal"));
		}
	}

	/// Demande la fermeture de la fenêtre application (bouton quitter).
	void AuthUiPresenter::ImGuiRequestClose(engine::platform::Window& window)
	{
		window.RequestClose();
	}

// Stubs Linux/Mac — aucune UI d'auth sur ces plateformes.
#else

	void AuthUiPresenter::BuildModel_Login(RenderModel&) const {}

	void AuthUiPresenter::Update_LoginShortcuts(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&,
		bool /*usingNativeAuth*/, bool /*authUiImguiMode*/)
	{
	}

#endif
} // namespace engine::client
