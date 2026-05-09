// AUTH-UI.4 — Couche modèle pour l'écran d'erreur d'authentification.

// BuildModel_Error peuple le RenderModel avec le layout riche (inscription) ou générique selon la phase d'origine.
#include "src/client/AuthUi.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace engine::client
{
#if defined(_WIN32)

	/// Acquitte l'écran d'erreur et relance la soumission de la phase d'origine (ex. retour au formulaire d'inscription).
	void AuthUiPresenter::ImGuiAcknowledgeErrorScreen(const engine::core::Config& cfg)
	{
		if (m_phase != Phase::Error)
		{
			return;
		}
		SubmitCurrentPhase(cfg);
	}

	/// Peuple le modèle d'erreur : layout riche (variantes pastilles) si l'erreur vient de l'inscription, générique sinon.
	/// Classifie automatiquement le message d'erreur (login pris, mot de passe faible, e-mail, réseau) pour choisir la variante affichée.
	void AuthUiPresenter::BuildModel_Error(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.panel.error");
		const bool rich = (m_errorReturnPhase == Phase::Register);
		model.authErrorRichRegisterLayout = rich;
		model.authErrorPanelTitle = rich ? Tr("auth.error.register_impossible_title") : Tr("auth.error.generic_title");
		model.authErrorPanelSubtitle.clear();
		model.authErrorVersionBadge = Tr("auth.error.version_badge");
		model.infoPopupText = rich ? Tr("auth.error.register_info_popup") : std::string();
		model.authErrorFieldSectionLabel = Tr("auth.error.field_to_fix");
		model.authErrorFixSectionLabel = Tr("auth.error.how_to_fix");
		model.authErrorBackButtonLabel = Tr("auth.error.back_to_form");
		model.authErrorBackKeycap = Tr("auth.error.keycap_escape");
		model.authErrorRetryButtonLabel = Tr("auth.error.retry");
		model.authRegisterErrorVariants.clear();
		if (rich)
		{
			auto addVariant = [&model, this](const char* pillKey, const char* titleKey, const char* msgKey, const char* fieldKey,
								   const char* fixKey, bool warn) {
				AuthUiPresenter::AuthRegisterErrorVariantRow row{};
				row.pillLabel = Tr(std::string_view(pillKey));
				row.bannerTitle = Tr(std::string_view(titleKey));
				row.bannerMessage = Tr(std::string_view(msgKey));
				row.fieldLabel = Tr(std::string_view(fieldKey));
				row.fixHint = Tr(std::string_view(fixKey));
				row.warningBanner = warn;
				model.authRegisterErrorVariants.push_back(std::move(row));
			};
			addVariant("auth.error.variant.taken.pill", "auth.error.variant.taken.banner_title", "auth.error.variant.taken.banner_msg",
				"auth.error.variant.taken.field", "auth.error.variant.taken.fix", false);
			addVariant("auth.error.variant.weak.pill", "auth.error.variant.weak.banner_title", "auth.error.variant.weak.banner_msg",
				"auth.error.variant.weak.field", "auth.error.variant.weak.fix", false);
			addVariant("auth.error.variant.email.pill", "auth.error.variant.email.banner_title", "auth.error.variant.email.banner_msg",
				"auth.error.variant.email.field", "auth.error.variant.email.fix", false);
			addVariant("auth.error.variant.network.pill", "auth.error.variant.network.banner_title", "auth.error.variant.network.banner_msg",
				"auth.error.variant.network.field", "auth.error.variant.network.fix", true);
			if (!model.authRegisterErrorVariants.empty())
			{
				model.authRegisterErrorVariants.back().fieldLabel.clear();
			}

			const std::string& err = m_userErrorText;
			auto lower = err;
			for (char& c : lower)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			auto hasSub = [&lower](std::string_view s) -> bool { return lower.find(s) != std::string::npos; };

			int classified = 3;
			if (err == Tr("auth.error.password_mismatch"))
			{
				classified = 1;
			}
			else if (err == Tr("auth.error.enter_register_fields") || err == Tr("auth.error.invalid_birth_date")
				|| err == Tr("auth.error.enter_country"))
			{
				classified = 0;
			}
			else if (hasSub("login already taken") || hasSub("already taken") || hasSub("invalid login"))
			{
				classified = 0;
			}
			else if (hasSub("weak password"))
			{
				classified = 1;
			}
			else if (hasSub("invalid email"))
			{
				classified = 2;
			}
			else if (hasSub("timeout") || hasSub("network error") || hasSub("connect failed") || hasSub("register worker")
				|| hasSub("send verify") || hasSub("send register") || hasSub("server error") || hasSub("internal error"))
			{
				classified = 3;
			}
			else if (hasSub("failed") || hasSub("exception"))
			{
				classified = 3;
			}
			else
			{
				classified = 3;
			}

			const bool bulkFieldHint = (err == Tr("auth.error.enter_register_fields"));
			const bool inferredNetwork = hasSub("timeout") || hasSub("network error") || hasSub("connect failed")
				|| hasSub("register worker") || hasSub("server error") || hasSub("internal error");
			model.authRegisterErrorClassifiedIndex = std::clamp(classified, 0, 3);
			model.authErrorBannerBodyFromUserMessage =
				(classified == 0 && (bulkFieldHint || err == Tr("auth.error.invalid_birth_date") || err == Tr("auth.error.enter_country")))
				|| (classified == 3 && !inferredNetwork);
			model.authErrorHideFieldBox = model.authErrorBannerBodyFromUserMessage && classified == 0;
			model.authErrorShowRetryButton = (classified == 3 && inferredNetwork);

			model.authRegisterCrumbLabels.clear();
			model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.lang"));
			model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.account"));
			model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.email"));
			model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.world"));
			model.authRegisterCrumbCurrent = 1;
		}
		else
		{
			model.authRegisterErrorClassifiedIndex = 0;
			model.authErrorBannerBodyFromUserMessage = false;
			model.authErrorHideFieldBox = false;
			model.authErrorShowRetryButton = false;
		}

		RenderAction action{};
		action.labelKey = "auth.error.back_to_form";
		action.primary = true;
		action.active = true;
		action.emphasized = false;
		action.hovered = static_cast<int32_t>(model.actions.size()) == m_hoveredActionIndex;
		model.actions.push_back(std::move(action));
	}

// Stubs Linux/Mac — aucune UI d'auth sur ces plateformes.
#else

	void AuthUiPresenter::ImGuiAcknowledgeErrorScreen(const engine::core::Config&) {}

	void AuthUiPresenter::BuildModel_Error(RenderModel&) const {}

#endif
} // namespace engine::client
