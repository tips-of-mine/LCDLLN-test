#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace engine::server
{
	class NetServer;
	class AccountStore;
	class SessionManager;
	class RateLimitAndBan;
	class SecurityAuditLog;
	class ConnectionSessionMap;
	class PasswordResetStore;
	class CaptchaVerifier;   ///< M33.3
	struct SmtpConfig;

	/// Handles AUTH_REQUEST and REGISTER_REQUEST opcodes: validate, store/session, rate-limit, audit, send response.
	/// Uses request_id and session_id from packet header for responses. Single-threaded worker use.
	class AuthRegisterHandler
	{
	public:
		AuthRegisterHandler() = default;

		/// Set dependencies. Call before HandlePacket. Null rateLimit or auditLog skips that step.
		void SetServer(NetServer* server);
		void SetAccountStore(AccountStore* store);
		void SetSessionManager(SessionManager* sessionManager);
		void SetRateLimitAndBan(RateLimitAndBan* rateLimit);
		void SetSecurityAuditLog(SecurityAuditLog* auditLog);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		/// M33.2: Set password reset / verification store (optional; if null, email verification is skipped).
		void SetPasswordResetStore(PasswordResetStore* resetStore);
		/// M33.2: Set SMTP configuration (optional; if null or host empty, emails are not sent).
		void SetSmtpConfig(const SmtpConfig* smtpConfig);
		/// M33.3: Set CAPTCHA verifier (optional; if null or not enabled, CAPTCHA check is skipped).
		void SetCaptchaVerifier(CaptchaVerifier* captchaVerifier);

		/// Handle one packet. Dispatches by opcode; sends response via NetServer::Send. Ignores unknown opcodes.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

		/// M23.2: counters for Prometheus auth_success_total / auth_fail_total. Thread-safe.
		uint64_t GetAuthSuccessTotal() const { return m_authSuccessTotal.load(std::memory_order_relaxed); }
		uint64_t GetAuthFailTotal() const { return m_authFailTotal.load(std::memory_order_relaxed); }

	private:
		void HandleRegister(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);
		void HandleAuth(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);
		void HandleUsernameAvailable(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

		NetServer* m_server = nullptr;
		AccountStore* m_accountStore = nullptr;
		SessionManager* m_sessionManager = nullptr;
		RateLimitAndBan* m_rateLimit = nullptr;
		SecurityAuditLog* m_auditLog = nullptr;
		ConnectionSessionMap* m_connectionSessionMap = nullptr;
		PasswordResetStore* m_resetStore = nullptr;        ///< M33.2
		const SmtpConfig*   m_smtpConfig = nullptr;       ///< M33.2
		CaptchaVerifier*    m_captchaVerifier = nullptr;  ///< M33.3
		std::atomic<uint64_t> m_authSuccessTotal{ 0 };
		std::atomic<uint64_t> m_authFailTotal{ 0 };
	};

}
