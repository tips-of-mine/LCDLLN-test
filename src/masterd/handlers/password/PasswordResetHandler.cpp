// M33.2 — PasswordResetHandler implementation.

#include "src/masterd/handlers/password/PasswordResetHandler.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/account/AccountStore.h"
#include "src/masterd/handlers/password/PasswordResetStore.h"
#include "src/masterd/email/SmtpMailer.h"
#include "src/shared/security/RateLimitAndBan.h"
#include "src/shared/security/SecurityAuditLog.h"
#include "src/masterd/account/AccountValidation.h"
#include "src/masterd/email/LocalizedEmail.h"
#include "src/shared/auth/Argon2Hash.h"
#include "src/shared/network/AuthRegisterPayloads.h"
#include "src/shared/network/NetErrorCode.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/core/Log.h"

#include <string>

namespace engine::server
{
	void PasswordResetHandler::SetServer(NetServer* server)            { m_server = server; }
	void PasswordResetHandler::SetAccountStore(AccountStore* store) { m_accountStore = store; }
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
		if (opcode == kOpcodeResendVerificationRequest)
		{
			HandleResendVerification(connId, requestId, sessionIdHeader, payload, payloadSize);
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
			bool isHtml = false;
			BuildPasswordResetEmail(optAccount->email_locale, resetUrl, subject, body, isHtml);
			LOG_INFO(Auth, "[PasswordResetHandler] envoi email reset mot de passe (account_id={})", account_id);
			const bool sent = SmtpMailer::Send(*m_smtpConfig, email, subject, body, isHtml);
			if (!sent)
				LOG_WARN(Auth,
					"[PasswordResetHandler] ForgotPassword: échec envoi email (account_id={}) — lignes [SMTP] / sous-système Smtp (log.level Info conseillé)",
					account_id);
			else
			{
				m_resetStore->RecordEmailSent(account_id);
				LOG_INFO(Auth, "[PasswordResetHandler] email reset envoyé (account_id={})", account_id);
			}
		}
		else
		{
			// Audit 2026-05-18 : avant ce fix, on logguait `token.substr(0, 8)` en
			// clair quand SMTP n'etait pas configure. Quiconque a access aux logs
			// pouvait prendre le controle du compte. On loggue uniquement la
			// longueur et un hash court (non reversible) pour debugger sans risque.
			LOG_WARN(Auth, "[PasswordResetHandler] ForgotPassword: SMTP not configured, reset token not emailed (account_id={} token_len={})",
				account_id, token.size());
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

		// Audit 2026-05-18 : avant ce fix, on differenciait ACCOUNT_NOT_FOUND
		// d'une mauvaise verification -> enumeration possible des account_id en
		// brute-forcant l'API VerifyEmail. Et il n'y avait AUCUN rate-limit sur
		// les tentatives de code 6-chiffres (espace 10^6, brute-force en heures).
		//
		// Fix : reponses uniformes (VERIFICATION_CODE_INVALID pour les trois cas
		// "compte inexistant" / "deja verifie" / "mauvais code"), et application
		// du rate-limit `m_rateLimit->TryConsumeAuth(ipKey)` partage avec le
		// flux login (10 tentatives/minute par IP). VERIFICATION_CODE_INVALID
		// reste informatif pour l'utilisateur legitime qui a juste mal tape.
		if (m_rateLimit)
		{
			const std::string ipKey = "conn:" + std::to_string(connId);
			if (!m_rateLimit->TryConsumeAuth(ipKey))
			{
				LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail rate-limited (connId={})", connId);
				auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::VERIFICATION_CODE_INVALID, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
		}

		// Lookup account to confirm it exists. NE PAS leaker au client.
		auto optAccount = m_accountStore->FindByAccountId(account_id);
		if (!optAccount)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: account not found (account_id={})", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::VERIFICATION_CODE_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			if (m_rateLimit) m_rateLimit->RecordAuthFailure("conn:" + std::to_string(connId));
			return;
		}

		// Already verified : on renvoie aussi le code generique pour ne pas leaker.
		if (optAccount->email_verified)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: already verified (account_id={})", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::VERIFICATION_CODE_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// Validate code.
		if (!m_resetStore->ValidateVerificationCode(account_id, parsed->code))
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: invalid or expired code (account_id={})", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::VERIFICATION_CODE_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			if (m_rateLimit) m_rateLimit->RecordAuthFailure("conn:" + std::to_string(connId));
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

	// ---------------------------------------------------------------------------
	// ResendVerification: generate a new 6-digit code and send a new verification email.
	// ---------------------------------------------------------------------------
	void PasswordResetHandler::HandleResendVerification(uint32_t connId, uint32_t requestId,
	                                                    uint64_t sessionIdHeader,
	                                                    const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Auth, "[PasswordResetHandler] HandleResendVerification connId={}", connId);

		if (!m_server || !m_accountStore || !m_resetStore)
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ResendVerification: missing required dependency");
			return;
		}

		auto parsed = ParseResendVerificationRequestPayload(payload, payloadSize);
		if (!parsed || parsed->account_id == 0)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ResendVerification: invalid payload (connId={})", connId);
			auto pkt = BuildResendVerificationResponseErrorPacket(NetErrorCode::BAD_REQUEST, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		const uint64_t account_id = parsed->account_id;

		// Audit 2026-05-18 : reponses uniformes pour ne pas leaker l'existence
		// de l'account_id. Pour l'utilisateur legitime, le fait que le mail
		// arrive (ou pas) est le vrai signal. Pour un attaquant, l'API ne
		// donne plus d'information differentielle.
		auto optAccount = m_accountStore->FindByAccountId(account_id);
		if (!optAccount)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ResendVerification: account not found (account_id={})", account_id);
			// Reponse OK simulee : on consomme silencieusement la requete.
			auto pkt = BuildResendVerificationResponsePacket(requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		if (optAccount->email_verified)
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ResendVerification: already verified (account_id={})", account_id);
			// Idem : OK simulee, on n'envoie pas de mail.
			auto pkt = BuildResendVerificationResponsePacket(requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		if (optAccount->email.empty())
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ResendVerification: account has no email (account_id={})", account_id);
			auto pkt = BuildResendVerificationResponseErrorPacket(NetErrorCode::BAD_REQUEST, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		if (!m_resetStore->CanSendEmail(account_id))
		{
			LOG_WARN(Auth, "[PasswordResetHandler] ResendVerification: rate limit hit (account_id={})", account_id);
			auto pkt = BuildResendVerificationResponseErrorPacket(NetErrorCode::BAD_REQUEST, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		const std::string code = m_resetStore->CreateVerificationCode(account_id);
		if (code.empty())
		{
			LOG_ERROR(Auth, "[PasswordResetHandler] ResendVerification: code generation failed (account_id={})", account_id);
			auto pkt = BuildResendVerificationResponseErrorPacket(NetErrorCode::INTERNAL_ERROR, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		m_accountStore->PersistEmailVerificationCode(account_id, code);

		if (m_smtpConfig && !m_smtpConfig->host.empty())
		{
			std::string subject, body;
			bool isHtml = false;
			BuildVerificationEmail(optAccount->email_locale, code, subject, body, isHtml);
			const bool sent = SmtpMailer::Send(*m_smtpConfig, optAccount->email, subject, body, isHtml);
			if (sent)
			{
				m_resetStore->RecordEmailSent(account_id);
				LOG_INFO(Auth, "[PasswordResetHandler] ResendVerification: email sent (account_id={})", account_id);
			}
			else
			{
				LOG_WARN(Auth, "[PasswordResetHandler] ResendVerification: SMTP send failed (account_id={})", account_id);
			}
		}
		else
		{
			// Audit 2026-05-18 : avant ce fix, on logguait le code de verif EN
			// CLAIR quand SMTP n'etait pas configure ("code de secours dev").
			// Quiconque a access aux logs pouvait verifier l'email du compte.
			// On loggue seulement la longueur ; le code reste accessible en base
			// pour les ops s'ils en ont vraiment besoin (devrait pas arriver en prod).
			LOG_WARN(Auth,
				"[PasswordResetHandler] ResendVerification: SMTP absent (account_id={} code_len={}) — verifier la config SMTP, code persiste en DB",
				account_id, code.size());
		}

		auto pkt = BuildResendVerificationResponsePacket(requestId, sessionIdHeader);
		if (!pkt.empty()) m_server->Send(connId, pkt);
		LOG_INFO(Auth, "[PasswordResetHandler] ResendVerification OK (account_id={} connId={})", account_id, connId);
	}

} // namespace engine::server
