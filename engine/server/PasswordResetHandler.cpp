// M33.2 — PasswordResetHandler implementation.

#include "engine/server/PasswordResetHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/InMemoryAccountStore.h"
#include "engine/server/PasswordResetStore.h"
#include "engine/server/SmtpMailer.h"
#include "engine/server/RateLimitAndBan.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/server/AccountValidation.h"
#include "engine/server/LocalizedEmail.h"
#include "engine/auth/Argon2Hash.h"
#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/NetErrorCode.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Log.h"

#include <string>

namespace engine::server
{
	void PasswordResetHandler::SetServer(NetServer* server)            { m_server = server; }
	void PasswordResetHandler::SetAccountStore(InMemoryAccountStore* store) { m_accountStore = store; }
	void PasswordResetHandler::SetPasswordResetStore(PasswordResetStore* rs) { m_resetStore = rs; }
	void PasswordResetHandler::SetSmtpConfig(const SmtpConfig* cfg)   { m_smtpConfig = cfg; }
	void PasswordResetHandler::SetRateLimitAndBan(RateLimitAndBan* rl) { m_rateLimit = rl; }
	void PasswordResetHandler::SetSecurityAuditLog(SecurityAuditLog* al) { m_auditLog = al; }

	void PasswordResetHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
	                                        uint64_t sessionIdHeader,
	                                        const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode == kOpcodeForgotPasswordRequest)
		{
			HandleForgotPassword(connId, requestId, sessionIdHeader, payload, payloadSize);
			return;
		}
		if (opcode == kOpcodeResetPasswordRequest)
		{
			HandleResetPassword(connId, requestId, sessionIdHeader, payload, payloadSize);
			return;
		}
		if (opcode == kOpcodeVerifyEmailRequest)
		{
			HandleVerifyEmail(connId, requestId, sessionIdHeader, payload, payloadSize);
			return;
		}
	}

	// ---------------------------------------------------------------------------
	// ForgotPassword: generate reset token, send email with reset link.
	// Always responds with success to avoid leaking whether an email is registered.
	// ---------------------------------------------------------------------------
	void PasswordResetHandler::HandleForgotPassword(uint32_t connId, uint32_t requestId,
	                                                uint64_t sessionIdHeader,
	                                                const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Auth, "[PasswordResetHandler] HandleForgotPassword connId={}", connId);

		if (!m_server || !m_accountStore || !m_resetStore)
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ForgotPassword: missing required dependency");
			return;
		}

		auto parsed = ParseForgotPasswordRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ForgotPassword: invalid payload (connId={})", connId);
			// Still send a success response to avoid enumeration.
			auto pkt = BuildForgotPasswordResponsePacket(requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		std::string email = NormaliseEmail(parsed->email);
		if (email.empty())
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ForgotPassword: empty email (connId={})", connId);
			auto pkt = BuildForgotPasswordResponsePacket(requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Always return success immediately to prevent email enumeration.
		auto pkt = BuildForgotPasswordResponsePacket(requestId, sessionIdHeader);
		if (!pkt.empty()) m_server->Send(connId, pkt);

		// Lookup account by email. No response difference on miss.
		auto optAccount = m_accountStore->FindByEmail(email);
		if (!optAccount)
		{
			LOG_DEBUG(Auth, "[PasswordResetHandler] ForgotPassword: email not found (no-op)");
			return;
		}

		const uint64_t account_id = optAccount->account_id;

		// Rate-limit email sends per account.
		if (!m_resetStore->CanSendEmail(account_id))
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ForgotPassword: email rate limit hit (account_id={})", account_id);
			return;
		}

		// Generate and store reset token.
		std::string token = m_resetStore->CreateResetToken(account_id);
		if (token.empty())
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ForgotPassword: CreateResetToken failed (account_id={})", account_id);
			return;
		}

		// Send email if SMTP is configured.
		if (m_smtpConfig && !m_smtpConfig->host.empty())
		{
			const std::string resetUrl = m_smtpConfig->reset_url_base + "?token=" + token;
			std::string subject;
			std::string body;
			BuildPasswordResetEmail(optAccount->email_locale, resetUrl, subject, body);
			bool sent = SmtpMailer::Send(*m_smtpConfig, email, subject, body);
			if (!sent)
				LOG_WARN(Auth, "[PasswordResetHandler] ForgotPassword: email send failed (account_id={})", account_id);
			else
				m_resetStore->RecordEmailSent(account_id);
		}
		else
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ForgotPassword: SMTP not configured, reset token not emailed (account_id={} token_prefix={}...)",
				account_id, token.size() >= 8u ? token.substr(0, 8) : token);
		}

		if (m_auditLog)
			m_auditLog->LogLoginFail("conn:" + std::to_string(connId), "forgot_password_requested");
	}

	// ---------------------------------------------------------------------------
	// ResetPassword: validate token, hash new password, update account.
	// ---------------------------------------------------------------------------
	void PasswordResetHandler::HandleResetPassword(uint32_t connId, uint32_t requestId,
	                                               uint64_t sessionIdHeader,
	                                               const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Auth, "[PasswordResetHandler] HandleResetPassword connId={}", connId);

		if (!m_server || !m_accountStore || !m_resetStore)
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ResetPassword: missing required dependency");
			return;
		}

		auto parsed = ParseResetPasswordRequestPayload(payload, payloadSize);
		if (!parsed || parsed->token.empty() || parsed->new_client_hash.empty())
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ResetPassword: invalid payload (connId={})", connId);
			auto pkt = BuildResetPasswordResponseErrorPacket(NetErrorCode::BAD_REQUEST, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Validate token.
		auto optAccountId = m_resetStore->ValidateResetToken(parsed->token);
		if (!optAccountId)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ResetPassword: invalid or expired token (connId={})", connId);
			// Distinguish expired vs invalid via ValidateResetToken log; always return TOKEN_INVALID to client.
			auto pkt = BuildResetPasswordResponseErrorPacket(NetErrorCode::TOKEN_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		const uint64_t account_id = *optAccountId;

		// Re-hash the new client_hash with a fresh server salt.
		auto server_salt = engine::auth::GenerateSalt();
		if (server_salt.empty())
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ResetPassword: GenerateSalt failed (account_id={})", account_id);
			auto pkt = BuildResetPasswordResponseErrorPacket(NetErrorCode::INTERNAL_ERROR, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		engine::auth::Argon2Params params;
		std::string new_final_hash = engine::auth::Hash(parsed->new_client_hash, server_salt, params);
		if (new_final_hash.empty())
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ResetPassword: Hash failed (account_id={})", account_id);
			auto pkt = BuildResetPasswordResponseErrorPacket(NetErrorCode::INTERNAL_ERROR, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Update stored password hash.
		if (!m_accountStore->UpdatePasswordHash(account_id, new_final_hash))
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ResetPassword: UpdatePasswordHash failed (account_id={})", account_id);
			auto pkt = BuildResetPasswordResponseErrorPacket(NetErrorCode::INTERNAL_ERROR, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Consume the token (single-use).
		m_resetStore->MarkResetTokenUsed(parsed->token);

		const uint8_t one = 1;
		auto pkt = BuildResetPasswordResponsePacket(one, requestId, sessionIdHeader);
		if (!pkt.empty()) m_server->Send(connId, pkt);

		LOG_INFO(Auth, "[PasswordResetHandler] Password reset success (account_id={} connId={})", account_id, connId);
	}

	// ---------------------------------------------------------------------------
	// VerifyEmail: validate 6-digit code, mark account email verified.
	// ---------------------------------------------------------------------------
	void PasswordResetHandler::HandleVerifyEmail(uint32_t connId, uint32_t requestId,
	                                             uint64_t sessionIdHeader,
	                                             const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Auth, "[PasswordResetHandler] HandleVerifyEmail connId={}", connId);

		if (!m_server || !m_accountStore || !m_resetStore)
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] VerifyEmail: missing required dependency");
			return;
		}

		auto parsed = ParseVerifyEmailRequestPayload(payload, payloadSize);
		if (!parsed || parsed->account_id == 0 || parsed->code.empty())
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: invalid payload (connId={})", connId);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::BAD_REQUEST, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		const uint64_t account_id = parsed->account_id;

		// Lookup account to confirm it exists.
		auto optAccount = m_accountStore->FindByAccountId(account_id);
		if (!optAccount)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: account not found (account_id={})", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::ACCOUNT_NOT_FOUND, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Already verified — idempotent success.
		if (optAccount->email_verified)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: already verified (account_id={})", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::EMAIL_ALREADY_VERIFIED, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Validate code.
		if (!m_resetStore->ValidateVerificationCode(account_id, parsed->code))
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: invalid or expired code (account_id={})", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::VERIFICATION_CODE_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Mark verified in both stores.
		m_resetStore->MarkEmailVerified(account_id);
		m_accountStore->SetEmailVerified(account_id);

		const uint8_t one = 1;
		auto pkt = BuildVerifyEmailResponsePacket(one, requestId, sessionIdHeader);
		if (!pkt.empty()) m_server->Send(connId, pkt);

		LOG_INFO(Auth, "[PasswordResetHandler] Email verified (account_id={} connId={})", account_id, connId);
	}

} // namespace engine::server
