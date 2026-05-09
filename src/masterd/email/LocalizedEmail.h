#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Supported languages for transactional emails (registration / password reset).
	enum class AccountEmailLocale : uint8_t
	{
		English = 0,
		French = 1,
		Spanish = 2,
		German = 3,
		Portuguese = 4,
		Italian = 5,
	};

	/// Maps client tag (e.g. "fr", "pt-BR") to a supported locale; unknown → English.
	AccountEmailLocale ParseAccountEmailLocale(std::string_view tag);

	/// Configure le répertoire racine des templates HTML d'email.
	/// Doit être appelé avant tout Build*Email (non thread-safe, appel unique au démarrage).
	void SetEmailTemplateDir(std::string_view dir);

	/// Subject + body for email verification (6-digit code).
	/// \param outIsHtml Set to true if the body is HTML (loaded from template), false for plain text fallback.
	void BuildVerificationEmail(AccountEmailLocale loc, const std::string& code,
	                            std::string& outSubject, std::string& outBody, bool& outIsHtml);

	/// Subject + body for password reset (link URL already built).
	/// \param outIsHtml Set to true if the body is HTML (loaded from template), false for plain text fallback.
	void BuildPasswordResetEmail(AccountEmailLocale loc, const std::string& resetUrl,
	                             std::string& outSubject, std::string& outBody, bool& outIsHtml);

	/// Confirmation e-mail after CGU / terms acceptance (audit trail).
	/// \param outIsHtml Set to true if the body is HTML (loaded from template), false for plain text fallback.
	void BuildTermsAcceptanceEmail(AccountEmailLocale loc, const std::string& versionLabel,
	                               std::string& outSubject, std::string& outBody, bool& outIsHtml);
}
