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

	/// Subject + body for email verification (6-digit code).
	void BuildVerificationEmail(AccountEmailLocale loc, const std::string& code, std::string& outSubject, std::string& outBody);

	/// Subject + body for password reset (link URL already built).
	void BuildPasswordResetEmail(AccountEmailLocale loc, const std::string& resetUrl, std::string& outSubject, std::string& outBody);

	/// Confirmation e-mail after CGU / terms acceptance (audit trail).
	void BuildTermsAcceptanceEmail(AccountEmailLocale loc, const std::string& versionLabel, std::string& outSubject, std::string& outBody);
}
