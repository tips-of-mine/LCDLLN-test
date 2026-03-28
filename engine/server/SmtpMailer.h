#pragma once

// M33.2 — SMTP mailer for password reset links and email verification codes.
// Configuration is loaded from config.json (smtp.*).
// On UNIX: minimal SMTP client using POSIX sockets + optional STARTTLS via OpenSSL.
// On Windows: stub — logs a warning and returns false (no SMTP support).

#include <cstdint>
#include <string>

namespace engine::core { class Config; }

namespace engine::server
{
	/// SMTP connection configuration. Load via SmtpConfig::Load(config).
	struct SmtpConfig
	{
		std::string host;            ///< SMTP server hostname (config: smtp.host). Empty = disabled.
		uint16_t    port       = 587;///< SMTP port (config: smtp.port; 587=STARTTLS, 25=plain).
		std::string user;            ///< AUTH LOGIN username (config: smtp.user).
		std::string password;        ///< AUTH LOGIN password (config: smtp.password).
		std::string from_address;    ///< From: header address (config: smtp.from).
		bool        use_starttls = true; ///< Enable STARTTLS upgrade (config: smtp.starttls).
		int         timeout_sec  = 10;  ///< Socket connect/read timeout in seconds (config: smtp.timeout_sec).
		std::string reset_url_base;  ///< Base URL for password reset links (config: smtp.reset_url_base).

		/// Loads SMTP config from config.json smtp.* keys. Returns default-constructed (disabled) on error.
		static SmtpConfig Load(const engine::core::Config& config);
	};

	/// Sends a plain-text email via SMTP.
	/// Thread-safe to call concurrently (each call opens its own connection).
	/// Returns true on success, false on any network/protocol error (always logs).
	class SmtpMailer
	{
	public:
		/// Sends one email.
		/// \param cfg    SMTP connection parameters.
		/// \param to     Recipient address (e.g. "user@example.com").
		/// \param subject Email subject line.
		/// \param body   Plain-text body.
		/// \return true if the server accepted the message, false otherwise.
		static bool Send(const SmtpConfig& cfg,
		                 const std::string& to,
		                 const std::string& subject,
		                 const std::string& body);
	};

} // namespace engine::server
