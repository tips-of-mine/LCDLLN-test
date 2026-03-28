#pragma once

// M33.2 — Handler for password reset and email verification opcodes.
// Opcodes handled: kOpcodeForgotPasswordRequest (21), kOpcodeResetPasswordRequest (23), kOpcodeVerifyEmailRequest (25).

#include <cstdint>
#include <string_view>

namespace engine::server
{
	class NetServer;
	class InMemoryAccountStore;
	class PasswordResetStore;
	class RateLimitAndBan;
	class SecurityAuditLog;
	struct SmtpConfig;

	/// Handles password reset and email verification request opcodes (M33.2).
	/// Dependencies must be set before the first HandlePacket call.
	/// Null optional dependencies (rateLimit, auditLog, smtpConfig) skip those steps.
	class PasswordResetHandler
	{
	public:
		PasswordResetHandler() = default;

		/// Set network server (required).
		void SetServer(NetServer* server);
		/// Set account store (required for all operations).
		void SetAccountStore(InMemoryAccountStore* store);
		/// Set password reset + verification code store (required).
		void SetPasswordResetStore(PasswordResetStore* resetStore);
		/// Set SMTP config for sending emails (optional; if null, emails are skipped with a warning).
		void SetSmtpConfig(const SmtpConfig* smtpConfig);
		/// Set rate-limit guard (optional).
		void SetRateLimitAndBan(RateLimitAndBan* rateLimit);
		/// Set security audit log (optional).
		void SetSecurityAuditLog(SecurityAuditLog* auditLog);

		/// Dispatches one packet by opcode. Ignores unknown opcodes.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

	private:
		/// Handles kOpcodeForgotPasswordRequest: generate token, send email.
		void HandleForgotPassword(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		                          const uint8_t* payload, size_t payloadSize);

		/// Handles kOpcodeResetPasswordRequest: validate token, update password hash.
		void HandleResetPassword(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		                         const uint8_t* payload, size_t payloadSize);

		/// Handles kOpcodeVerifyEmailRequest: validate code, activate account.
		void HandleVerifyEmail(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		                       const uint8_t* payload, size_t payloadSize);

		NetServer*           m_server       = nullptr;
		InMemoryAccountStore* m_accountStore = nullptr;
		PasswordResetStore*  m_resetStore   = nullptr;
		const SmtpConfig*    m_smtpConfig   = nullptr;
		RateLimitAndBan*     m_rateLimit    = nullptr;
		SecurityAuditLog*    m_auditLog     = nullptr;
	};

} // namespace engine::server
