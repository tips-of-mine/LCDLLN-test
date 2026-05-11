#include "src/client/auth/AuthUi.h"
#include "src/client/render/AuthUiRenderer.h"
#include "src/shared/core/DefaultClientEndpoints.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/platform/Window.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace engine::client
{
namespace
{
	constexpr std::string_view kLoginBackgroundPath = "ui/loading/background.png";
	constexpr std::string_view kLoginLogoPath = "";
	constexpr std::string_view kRegisterBackgroundPath = "ui/loading/background.png";
	constexpr std::string_view kRegisterInfoPath = "ui/register/info.png";
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
}

bool AuthUiPresenter::HandleNativeAuthScreen(engine::platform::Window& window, const engine::core::Config& cfg)
{
#if defined(_WIN32)
	auto phaseName = [](Phase phase) -> const char*
	{
		switch (phase)
		{
		case Phase::Login: return "Login";
		case Phase::Register: return "Register";
		case Phase::ForgotPassword: return "ForgotPassword";
		case Phase::VerifyEmail: return "VerifyEmail";
		case Phase::EmailConfirmationPending: return "EmailConfirmationPending";
		case Phase::LanguageSelectionFirstRun: return "LanguageSelectionFirstRun";
		case Phase::LanguageOptions: return "LanguageOptions";
		case Phase::Submitting: return "Submitting";
		case Phase::Error: return "Error";
		default: return "Unknown";
		}
	};

	if (m_phase == Phase::Login || m_phase == Phase::ForgotPassword || m_phase == Phase::Register)
	{
		if (m_phase == Phase::Login)
		{
			m_login = window.GetAuthPrimaryValue();
			m_password = window.GetAuthPasswordValue();
			m_rememberLogin = window.GetAuthRememberChecked();
			if (m_rememberLogin != m_savedRememberLogin)
			{
				SaveRememberPreference();
			}
		}
		else if (m_phase == Phase::ForgotPassword)
		{
			m_email = window.GetAuthPrimaryValue();
		}

		switch (window.ConsumeAuthScreenCommand())
		{
		case engine::platform::Window::AuthScreenCommand::Submit:
			LOG_INFO(Core, "[AuthUiPresenter] Submit requested (phase={}, login_empty={}, password_empty={}, email_empty={})",
				phaseName(m_phase), m_login.empty(), m_password.empty(), m_email.empty());
			SubmitCurrentPhase(cfg);
			break;
		case engine::platform::Window::AuthScreenCommand::Quit:
			window.RequestClose();
			break;
		case engine::platform::Window::AuthScreenCommand::OpenRegister:
			LOG_INFO(Core, "[AuthUiPresenter] Phase change: {} -> Register", phaseName(m_phase));
			SetPhase(Phase::Register);
			m_activeField = 0;
			m_userErrorText.clear();
			m_passwordConfirm.clear();
			// Plan C : réinitialiser état disponibilité username à l'entrée en Register.
			m_usernameCheckState = UsernameCheckState::Idle;
			m_usernameCheckSeq++;
			m_usernameDebounceTimer = 0.0;
			m_usernameLastChecked.clear();
			break;
		case engine::platform::Window::AuthScreenCommand::OpenForgotPassword:
		{
			const std::string resetUrl = ResolvePasswordRecoveryUrl(cfg);
			LOG_INFO(Core, "[AuthUiPresenter] Open password recovery portal from phase={} url={}", phaseName(m_phase), resetUrl);
			if (!window.OpenExternalUrl(resetUrl))
			{
				EnterAuthErrorPhase(Phase::Login, Tr("auth.error.open_recovery_portal"));
			}
			break;
		}
		case engine::platform::Window::AuthScreenCommand::BackToLogin:
			LOG_INFO(Core, "[AuthUiPresenter] Phase change: {} -> Login", phaseName(m_phase));
			if (m_phase == Phase::Register)
			{
				m_usernameCheckState = UsernameCheckState::Idle;
				m_usernameCheckSeq++;
				m_usernameDebounceTimer = 0.0;
				m_usernameLastChecked.clear();
			}
			SetPhase(Phase::Login);
			m_activeField = 0;
			m_userErrorText.clear();
			break;
		case engine::platform::Window::AuthScreenCommand::None:
		default:
			break;
		}

		engine::platform::Window::AuthScreenState state{};
		state.visible = true;
		state.showPassword = m_phase == Phase::Login;
		state.showRemember = m_phase == Phase::Login;
		state.showForgot = m_phase == Phase::Login;
		state.showRegister = m_phase == Phase::Login;
		state.showBack = false;
		state.showQuit = true;
		state.showInfoImage = m_phase == Phase::Register;
		state.rememberChecked = m_rememberLogin;
		state.focusPrimary = m_activeField == 0;
		state.focusPassword = (m_phase == Phase::Login) && (m_activeField == 1);
		state.titleLine1 = Tr("auth.title_line1");
		state.titleLine2 = Tr("auth.title_line2");
		state.sectionTitle = m_phase == Phase::Login ? Tr("auth.section.login")
			: (m_phase == Phase::ForgotPassword ? Tr("auth.section.forgot_password") : Tr("auth.section.register"));
		state.primaryLabel = m_phase == Phase::Register ? "" : Tr("common.login_or_email");
		state.primaryValue = (m_phase == Phase::Login) ? m_login : m_email;
		state.passwordLabel = Tr("auth.label.password");
		state.passwordValue = m_password;
		state.rememberLabel = Tr("auth.checkbox.remember");
		state.forgotLabel = Tr("auth.button.forgot_password");
		state.registerLabel = Tr("auth.button.register");
		state.submitLabel = m_phase == Phase::Register ? "" : Tr("common.submit");
		{
			std::string q = Tr("common.quit_desktop");
			if (q.empty())
				q = Tr("common.quit");
			state.quitLabel = std::move(q);
		}
		state.backgroundImagePath = m_phase == Phase::Register ? std::string(kRegisterBackgroundPath) : std::string(kLoginBackgroundPath);
		state.logoImagePath = m_phase == Phase::Login ? std::string(kLoginLogoPath) : "";
		state.infoImagePath = m_phase == Phase::Register ? std::string(kRegisterInfoPath) : "";
		window.SetAuthScreenState(state);
		return true;
	}
	window.SetAuthScreenState({});
	(void)cfg;
	return false;
#else
	(void)window;
	(void)cfg;
	return false;
#endif
}

}
