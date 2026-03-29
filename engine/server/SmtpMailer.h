#pragma once

// M33.2 — SMTP mailer for password reset links and email verification codes.
// Connection settings (host, port, credentials, from, reset URL) are loaded from a sidecar JSON
// file (see smtp.config_file in the main config, default: smtp.local.json next to config.json).
// On UNIX: minimal SMTP client using POSIX sockets + optional STARTTLS via OpenSSL.
// On Windows: stub — logs a warning and returns false (no SMTP support).

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::core { class Config; }

namespace engine::server
{
	/// SMTP connection configuration. Load via SmtpConfig::Load(mainConfig, primaryConfigPath).
	struct SmtpConfig
	{
		std::string host;            ///< SMTP server hostname (sidecar: smtp.host). Empty = disabled.
		uint16_t    port       = 0; ///< SMTP port (sidecar: smtp.port). Must be set when host is set.
		std::string user;            ///< AUTH LOGIN username (config: smtp.user).
		std::string password;        ///< AUTH LOGIN password (config: smtp.password).
		std::string from_address;    ///< From: header address (config: smtp.from).
		bool        use_starttls = true; ///< Enable STARTTLS upgrade (config: smtp.starttls).
		int         timeout_sec  = 10;  ///< Socket connect/read timeout in seconds (config: smtp.timeout_sec).
		std::string reset_url_base;  ///< Base URL for password reset links (config: smtp.reset_url_base).

		/// Loads SMTP settings from the sidecar file named by \a mainConfig `smtp.config_file`
		/// (default `smtp.local.json`), resolved next to \a primaryConfigPath (e.g. `config.json`).
		/// Main config must not contain SMTP secrets; only the path key is read from \a mainConfig.
		/// Returns default-constructed (disabled) if the file is missing or invalid.
		static SmtpConfig Load(const engine::core::Config& mainConfig, std::string_view primaryConfigPath);
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
