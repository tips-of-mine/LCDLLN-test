#pragma once

// M33.3 — Server-side CAPTCHA token verifier.
// Supports hCaptcha and reCAPTCHA v2/v3 via their respective secret-verify endpoints.
// In dev/bypass mode (enabled=false or trusted IP) all tokens are accepted without network call.
// On Windows the HTTP verification layer emits a LOG_WARN and falls back to bypass.
// Thread-safe only if caller serialises access (single-worker v1).

#include "engine/core/Config.h"

#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	/// CAPTCHA provider selection.
	enum class CaptchaProvider
	{
		HCaptcha  = 0, ///< https://hcaptcha.com  — verify at https://hcaptcha.com/siteverify
		ReCaptcha = 1, ///< https://google.com/recaptcha — verify at https://www.google.com/recaptcha/api/siteverify
	};

	/// Configuration for CaptchaVerifier.
	struct CaptchaConfig
	{
		/// Enable CAPTCHA verification (false = bypass/dev mode).
		bool enabled = false;
		/// Provider to use.
		CaptchaProvider provider = CaptchaProvider::HCaptcha;
		/// Secret key from the CAPTCHA dashboard.
		std::string secret_key;
		/// Override verify URL (empty = use provider default).
		std::string verify_url_override;
		/// IPs that bypass CAPTCHA (trusted devs, admins, internal services).
		std::vector<std::string> trusted_ips;
	};

	/// Server-side CAPTCHA token verifier.
	/// Wire this into AuthRegisterHandler to gate registrations behind a CAPTCHA challenge.
	class CaptchaVerifier
	{
	public:
		CaptchaVerifier() = default;

		/// Set configuration. Call before Verify.
		void SetConfig(const CaptchaConfig& config);

		/// Build config from engine Config
		/// (keys: captcha.enabled, captcha.provider ["hcaptcha"|"recaptcha"],
		///        captcha.secret_key, captcha.trusted_ips [comma-separated]).
		static CaptchaConfig LoadConfig(const engine::core::Config& config);

		/// Verify a CAPTCHA response token submitted by the client.
		/// \param token      CAPTCHA response token from the client widget.
		/// \param remoteIp   Client IP (forwarded to provider for fraud scoring, may be empty).
		/// Returns true when the token is valid (or verification is bypassed / IP trusted).
		/// Returns false when verification fails or encounters a network error.
		bool Verify(std::string_view token, std::string_view remoteIp) const;

		/// Returns true when CAPTCHA verification is enabled (not bypassed).
		bool IsEnabled() const { return m_config.enabled; }

	private:
		/// Returns true when \a ip is in the trusted-IP whitelist.
		bool IsTrustedIp(std::string_view ip) const;

		/// Returns the verification URL for the configured provider.
		std::string ResolveVerifyUrl() const;

		/// Perform an HTTP POST to the provider verify endpoint and parse the "success" field.
		/// Uses a blocking POSIX socket + OpenSSL handshake on UNIX.
		/// On Windows this logs a warning and returns true (bypass).
		bool VerifyHttp(std::string_view token, std::string_view remoteIp) const;

		CaptchaConfig m_config;
	};
}
