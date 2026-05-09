// AUTH-UI.5 — Couche modèle pour les écrans de vérification d'e-mail et de confirmation d'inscription.

// BuildModel_VerifyEmail : saisie du code pendant l'inscription.
// BuildModel_EmailConfirmationPending : page post-inscription avec bouton de renvoi après 15 min.
#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"

#include <cstring>
#include <string>
#include <string_view>

namespace engine::client
{
#if defined(_WIN32)

	/// Peuple le modèle pour la phase de saisie du code à 6 chiffres (pendant l'inscription).
	void AuthUiPresenter::BuildModel_VerifyEmail(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.phase.verify_email");
		{
			RenderField field{};
			field.label = Tr("auth.label.verify_code");
			field.value = m_verifyCode;
			field.active = true;
			field.hovered = (m_hoveredFieldIndex == 0);
			field.secret = false;
			field.cyclePicker = false;
			model.fields.push_back(std::move(field));
		}
		model.authRegisterCrumbLabels.clear();
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.lang"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.account"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.email"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.world"));
		model.authRegisterCrumbCurrent = 2;
		model.authVerifyPanelTitle = Tr("auth.verify.panel_title");
		model.authVerifyPanelSubtitle = Tr("auth.verify.subtitle", { { "email", m_email } });
		model.authVerifyPanelBadge = Tr("auth.verify.panel_badge");
		model.authVerifyInfoPopupText = Tr("auth.verify.info_popup");
		model.authVerifyDigitLabel = Tr("auth.verify.digit_label");
		model.authVerifyResendLabel = Tr("auth.verify.resend");
		model.authVerifyChangeEmailLabel = Tr("auth.verify.change_email");
		model.authVerifySubmitLabel = Tr("auth.verify.submit");
		model.authVerifyBackLabel = Tr("auth.verify.back");
		model.authVerifyBackKeycap = Tr("auth.verify.back_keycap");
		model.authVerifySubmitKeycap = Tr("auth.verify.submit_keycap");
		model.authVerifyDevHint = Tr("auth.verify.dev_hint");
		model.authEmailConfirmationPendingPanel = false;
		model.authEmailConfirmationOkTitle.clear();
		model.authEmailConfirmationOkBody.clear();
		{
			RenderAction submit{};
			submit.labelKey = "common.submit";
			submit.primary = true;
			submit.active = true;
			submit.emphasized = false;
			submit.hovered = (m_hoveredActionIndex == 0);
			model.actions.push_back(std::move(submit));
		}
		{
			RenderAction back{};
			back.labelKey = "auth.hint.return_login";
			back.labelKeyFallback = "common.back";
			back.primary = false;
			back.active = true;
			back.emphasized = false;
			back.hovered = (m_hoveredActionIndex == 1);
			model.actions.push_back(std::move(back));
		}
	}

	/// Peuple le modèle pour la page post-inscription : affiche le bandeau "vérifiez vos e-mails" et active le bouton de renvoi après 15 min.
	void AuthUiPresenter::BuildModel_EmailConfirmationPending(RenderModel& model) const
	{
		using namespace std::chrono;
		const bool timerStarted = m_verifyCodeSentAt != steady_clock::time_point{};
		const bool codeExpired  = timerStarted
			&& (steady_clock::now() - m_verifyCodeSentAt) >= minutes(15);

		model.titleLine2 = Tr("auth.email_confirmation.title");
		model.sectionTitle = Tr("auth.panel.email_confirmation");
		model.infoBanner.clear();
		model.authEmailConfirmationPendingPanel = true;
		model.authEmailConfirmationOkTitle = Tr("auth.email_confirmation.banner_ok_title");
		model.authEmailConfirmationOkBody = Tr("auth.email_confirmation.banner_ok_body");
		model.authVerifyCanResend = codeExpired;
		model.authVerifyCodeExpiredMessage = codeExpired ? Tr("auth.email_confirmation.code_expired") : std::string{};
		{
			RenderField field{};
			field.label = Tr("auth.label.verify_code");
			field.value = m_verifyCode;
			field.active = true;
			field.hovered = (m_hoveredFieldIndex == 0);
			field.secret = false;
			field.cyclePicker = false;
			model.fields.push_back(std::move(field));
		}
		model.authRegisterCrumbLabels.clear();
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.lang"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.account"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.email"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.world"));
		model.authRegisterCrumbCurrent = 2;
		model.authVerifyPanelTitle = Tr("auth.email_confirmation.title");
		model.authVerifyPanelSubtitle = Tr("auth.verify.subtitle", { { "email", m_email } });
		model.authVerifyPanelBadge = Tr("auth.verify.panel_badge");
		model.authVerifyInfoPopupText = Tr("auth.verify.info_popup");
		model.authVerifyDigitLabel = Tr("auth.verify.digit_label");
		model.authVerifyResendLabel.clear();
		model.authVerifyChangeEmailLabel.clear();
		model.authVerifySubmitLabel = Tr("auth.verify.submit");
		model.authVerifyBackLabel = Tr("auth.email_confirmation.back_to_login");
		model.authVerifyBackKeycap = Tr("auth.verify.back_keycap");
		model.authVerifySubmitKeycap = Tr("auth.verify.submit_keycap");
		model.authVerifyDevHint = Tr("auth.verify.dev_hint");
		{
			const std::string text = Tr("auth.email_confirmation.message");
			if (!text.empty())
			{
				size_t start = 0;
				for (;;)
				{
					const size_t nl = text.find('\n', start);
					if (nl == std::string::npos)
					{
						const std::string chunk = text.substr(start);
						if (!chunk.empty())
						{
							RenderBodyLine line{};
							line.text = chunk;
							line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
							model.bodyLines.push_back(std::move(line));
						}
						break;
					}
					const std::string chunk = text.substr(start, nl - start);
					if (!chunk.empty())
					{
						RenderBodyLine line{};
						line.text = chunk;
						line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
						model.bodyLines.push_back(std::move(line));
					}
					start = nl + 1u;
				}
			}
		}
		if (!m_registeredTagId.empty())
		{
			RenderBodyLine tag{};
			tag.text = Tr("auth.info.tag_id") + " " + m_registeredTagId;
			tag.active = false;
			tag.link = false;
			tag.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
			model.bodyLines.push_back(std::move(tag));
		}
		{
			RenderAction submit{};
			submit.labelKey = "common.submit";
			submit.primary = true;
			submit.active = true;
			submit.emphasized = false;
			submit.hovered = (m_hoveredActionIndex == 0);
			model.actions.push_back(std::move(submit));
		}
		{
			RenderAction back{};
			back.labelKey = "auth.hint.return_login";
			back.labelKeyFallback = "common.back";
			back.primary = false;
			back.active = true;
			back.emphasized = false;
			back.hovered = (m_hoveredActionIndex == 1);
			model.actions.push_back(std::move(back));
		}
	}

	/// Gère Tab hors ImGui pour réinitialiser le focus sur les écrans de vérification / confirmation.
	void AuthUiPresenter::Update_VerifyEmail(engine::platform::Input& input, const engine::core::Config&, engine::platform::Window&,
		bool usingNativeAuth, bool authUiImguiMode)
	{
		if (usingNativeAuth || authUiImguiMode)
		{
			return;
		}
		if (m_phase != Phase::VerifyEmail && m_phase != Phase::EmailConfirmationPending)
		{
			return;
		}
		if (input.WasPressed(engine::platform::Key::Tab))
		{
			m_activeField = 0;
			LOG_DEBUG(Core, "[AuthUiPresenter] VerifyEmail/EmailConfirmation: Tab -> reset field focus token");
		}
	}

	/// Soumet le code de vérification (6 chiffres) depuis le renderer ImGui.
	void AuthUiPresenter::ImGuiSubmitVerifyEmailCode(const engine::core::Config& cfg, const char* codeSixUtf8)
	{
		if (m_phase != Phase::VerifyEmail && m_phase != Phase::EmailConfirmationPending)
		{
			return;
		}
		if (codeSixUtf8 != nullptr)
		{
			m_verifyCode = codeSixUtf8;
		}
		SubmitCurrentPhase(cfg);
	}

	/// Retour à l'écran de connexion depuis la phase VerifyEmail (abandon de la vérification en cours d'inscription).
	void AuthUiPresenter::ImGuiBackFromVerifyToLogin()
	{
		if (m_phase != Phase::VerifyEmail)
		{
			return;
		}
		m_registeredTagId.clear();
		m_passwordConfirm.clear();
		m_userErrorText.clear();
		m_verifyCode.clear();
		m_activeField = 0;
		// Vérification abandonnée : la prochaine connexion ne doit pas hériter du flag « post-inscription ».
		m_postRegistrationCharacterCreatePending = false;
		SetPhase(Phase::Login);
	}

	/// Efface les 6 chiffres saisis (lien « renvoyer » dans la maquette, sans appel réseau immédiat).
	void AuthUiPresenter::ImGuiVerifyEmailClearDigits()
	{
		if (m_phase != Phase::VerifyEmail)
		{
			return;
		}
		m_verifyCode.clear();
		LOG_INFO(Core, "[AuthUiPresenter] ImGui: code de verification efface (renvoi / nouvelle saisie)");
	}

	/// Retourne au formulaire d'inscription positionné sur le champ e-mail (correction de l'adresse).
	void AuthUiPresenter::ImGuiVerifyEmailBackToEditRegisterEmail()
	{
		if (m_phase != Phase::VerifyEmail)
		{
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] ImGui: retour inscription pour modifier le courriel");
		m_userErrorText.clear();
		m_activeField = 3u;
		SetPhase(Phase::Register);
		m_registeredTagId.clear();
		m_pendingVerifyAccountId = 0;
		m_verifyCode.clear();
	}

	/// Met à jour le code partiel depuis le renderer ImGui (filtre chiffres 0-9, max 6 caractères).
	void AuthUiPresenter::ImGuiSetVerifyEmailPartialCode(std::string_view s)
	{
		if (m_phase != Phase::VerifyEmail && m_phase != Phase::EmailConfirmationPending)
		{
			return;
		}
		m_verifyCode.clear();
		for (unsigned char c : s)
		{
			if (c >= '0' && c <= '9' && m_verifyCode.size() < 6u)
			{
				m_verifyCode.push_back(static_cast<char>(c));
			}
		}
	}

	/// Retour à la connexion depuis la phase EmailConfirmationPending.
	void AuthUiPresenter::ImGuiEmailConfirmationBackToLogin()
	{
		if (m_phase != Phase::EmailConfirmationPending)
		{
			return;
		}
		m_registeredTagId.clear();
		m_userErrorText.clear();
		m_activeField = 0;
		m_verifyCode.clear();
		// Confirmation courriel abandonnée : la prochaine connexion ne doit pas hériter du flag « post-inscription ».
		m_postRegistrationCharacterCreatePending = false;
		SetPhase(Phase::Login);
	}

	/// Déclenche le renvoi d'un nouveau code de vérification (opcode 37) — uniquement si le compte est en attente et l'ID connu.
	void AuthUiPresenter::ImGuiResendVerificationEmail(const engine::core::Config& cfg)
	{
		if (m_phase != Phase::EmailConfirmationPending && m_phase != Phase::VerifyEmail)
		{
			return;
		}
		if (m_pendingVerifyAccountId == 0)
		{
			return;
		}
		StartResendVerificationWorker(cfg);
	}

#else

	void AuthUiPresenter::BuildModel_VerifyEmail(RenderModel&) const {}

	void AuthUiPresenter::BuildModel_EmailConfirmationPending(RenderModel&) const {}

	void AuthUiPresenter::Update_VerifyEmail(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool) {}

	void AuthUiPresenter::ImGuiSubmitVerifyEmailCode(const engine::core::Config&, const char*) {}

	void AuthUiPresenter::ImGuiBackFromVerifyToLogin() {}

	void AuthUiPresenter::ImGuiVerifyEmailClearDigits() {}

	void AuthUiPresenter::ImGuiVerifyEmailBackToEditRegisterEmail() {}

	void AuthUiPresenter::ImGuiSetVerifyEmailPartialCode(std::string_view) {}

	void AuthUiPresenter::ImGuiEmailConfirmationBackToLogin() {}

	void AuthUiPresenter::ImGuiResendVerificationEmail(const engine::core::Config&) {}

#endif
} // namespace engine::client
