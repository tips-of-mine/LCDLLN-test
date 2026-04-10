#include "engine/server/AuthRegisterHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/InMemoryAccountStore.h"
#include "engine/server/SessionManager.h"
#include "engine/server/RateLimitAndBan.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/server/ConnectionSessionMap.h"
#include "engine/server/AccountValidation.h"
#include "engine/server/PasswordResetStore.h"
#include "engine/server/SmtpMailer.h"
#include "engine/server/CaptchaVerifier.h"    // M33.3
#include "engine/server/LocalizedEmail.h"
#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/ErrorPacket.h"
#include "engine/network/NetErrorCode.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/auth/Argon2Hash.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace engine::server
{
	namespace
	{
		std::string ConnIdToRateLimitKey(uint32_t connId)
		{
			return "conn:" + std::to_string(connId);
		}

		uint64_t ServerTimeSecondsSinceEpoch()
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}
	}

	void AuthRegisterHandler::SetServer(NetServer* server) { m_server = server; }
	void AuthRegisterHandler::SetAccountStore(InMemoryAccountStore* store) { m_accountStore = store; }
	void AuthRegisterHandler::SetSessionManager(SessionManager* sessionManager) { m_sessionManager = sessionManager; }
	void AuthRegisterHandler::SetRateLimitAndBan(RateLimitAndBan* rateLimit) { m_rateLimit = rateLimit; }
	void AuthRegisterHandler::SetSecurityAuditLog(SecurityAuditLog* auditLog) { m_auditLog = auditLog; }
	void AuthRegisterHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connectionSessionMap = map; }
	void AuthRegisterHandler::SetPasswordResetStore(PasswordResetStore* rs) { m_resetStore = rs; }
	void AuthRegisterHandler::SetSmtpConfig(const SmtpConfig* cfg) { m_smtpConfig = cfg; }
	void AuthRegisterHandler::SetCaptchaVerifier(CaptchaVerifier* cv) { m_captchaVerifier = cv; } // M33.3

	void AuthRegisterHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		if (!m_server)
		{
			LOG_WARN(Auth, "[AuthRegisterHandler] HandlePacket: no server set");
			return;
		}
		if (opcode == engine::network::kOpcodeHeartbeat)
		{
			if (sessionIdHeader != 0 && m_sessionManager)
				m_sessionManager->Touch(sessionIdHeader);
			return;
		}
		if (opcode == engine::network::kOpcodeRegisterRequest)
		{
			HandleRegister(connId, requestId, sessionIdHeader, payload, payloadSize);
			return;
		}
		if (opcode == engine::network::kOpcodeAuthRequest)
		{
			HandleAuth(connId, requestId, sessionIdHeader, payload, payloadSize);
			return;
		}
	}

	void AuthRegisterHandler::HandleRegister(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Auth, "[AUTH] HandleRegister connId={}", connId);
		const std::string ipKey = ConnIdToRateLimitKey(connId);
		if (m_rateLimit && !m_rateLimit->TryConsumeRegister(ipKey))
		{
			// Peut arriver en rafale si le client renvoie l’inscription en boucle ; éviter un WARN par paquet.
			LOG_DEBUG(Auth, "[AuthRegisterHandler] Register rate limited (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "rate limited", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto parsed = ParseRegisterRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Auth, "[AuthRegisterHandler] Register: invalid payload");
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid payload", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// M33.3: CAPTCHA verification on registration.
		if (m_captchaVerifier && m_captchaVerifier->IsEnabled())
		{
			const bool captchaOk = m_captchaVerifier->Verify(parsed->captcha_token, "");
			if (!captchaOk)
			{
				LOG_WARN(Auth, "[AuthRegisterHandler] Register: CAPTCHA verification failed (connId={})", connId);
				if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "captcha_failed");
				auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "captcha failed", requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
				return;
			}
			LOG_DEBUG(Auth, "[AuthRegisterHandler] Register: CAPTCHA OK (connId={})", connId);
		}
		std::string login_norm(NormaliseLoginView(parsed->login));
		if (login_norm.empty())
		{
			LOG_WARN(Auth, "[AUTH] HandleRegister result={}", "FAIL");
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "invalid_login");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::INVALID_LOGIN, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		auto loginErr = ValidateLogin(login_norm);
		if (loginErr != NetErrorCode::OK)
		{
			LOG_WARN(Auth, "[AUTH] HandleRegister result={}", "FAIL");
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "invalid_login");
			auto pkt = BuildRegisterResponseErrorPacket(loginErr, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		std::string email_norm = NormaliseEmail(parsed->email);
		if (!email_norm.empty())
		{
			auto emailErr = ValidateEmail(email_norm);
			if (emailErr != NetErrorCode::OK)
			{
				LOG_WARN(Auth, "[AUTH] HandleRegister result={}", "FAIL");
				if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "invalid_email");
				auto pkt = BuildRegisterResponseErrorPacket(emailErr, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
		}
		if (!m_accountStore)
		{
			LOG_ERROR(Auth, "[AuthRegisterHandler] Register: no account store");
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "unavailable", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (m_accountStore->ExistsLogin(login_norm))
		{
			LOG_WARN(Auth, "[AUTH] HandleRegister result={}", "FAIL");
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "login_taken");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::LOGIN_ALREADY_TAKEN, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (!email_norm.empty() && m_accountStore->ExistsEmail(email_norm))
		{
			LOG_WARN(Auth, "[AUTH] HandleRegister result={}", "FAIL");
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "email_taken");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::INVALID_EMAIL, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		const AccountEmailLocale emailLocale = ParseAccountEmailLocale(parsed->locale_tag);
		uint64_t account_id = m_accountStore->CreateAccount(
			login_norm,
			email_norm,
			parsed->client_hash,
			parsed->first_name,
			parsed->last_name,
			parsed->birth_date,
			emailLocale);
		if (account_id == 0)
		{
			LOG_WARN(Auth, "[AUTH] HandleRegister result={}", "FAIL");
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "create_failed");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::REGISTRATION_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (m_auditLog) m_auditLog->LogRegisterSuccess(ipKey, account_id);
		uint8_t one = 1;
		auto pkt = BuildRegisterResponsePacket(one, account_id, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
		LOG_INFO(Auth, "[AuthRegisterHandler] Register success (connId={}, account_id={})", connId, account_id);

		// M33.2: If email is provided, generate a verification code and send it.
		if (!email_norm.empty() && m_resetStore && m_smtpConfig && !m_smtpConfig->host.empty())
		{
			if (m_resetStore->CanSendEmail(account_id))
			{
				std::string code = m_resetStore->CreateVerificationCode(account_id);
				if (!code.empty())
				{
					std::string subject;
					std::string body;
					BuildVerificationEmail(emailLocale, code, subject, body);
					bool sent = SmtpMailer::Send(*m_smtpConfig, email_norm, subject, body);
					if (sent)
						m_resetStore->RecordEmailSent(account_id);
					else
						LOG_WARN(Auth, "[AuthRegisterHandler] Email verification send failed (account_id={})", account_id);
				}
				else
				{
					LOG_WARN(Auth, "[AuthRegisterHandler] CreateVerificationCode failed (account_id={})", account_id);
				}
			}
			else
			{
				LOG_WARN(Auth, "[AuthRegisterHandler] Email verification rate limit hit (account_id={})", account_id);
			}
		}
		else if (!email_norm.empty() && m_resetStore)
		{
			// SMTP not configured: generate code, log it (dev mode only).
			std::string code = m_resetStore->CreateVerificationCode(account_id);
			if (!code.empty())
				LOG_WARN(Auth, "[AuthRegisterHandler] SMTP disabled — verification code for account_id={}: {}", account_id, code);
		}
	}

	void AuthRegisterHandler::HandleAuth(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Auth, "[AUTH] HandleAuth connId={}", connId);
		const std::string ipKey = ConnIdToRateLimitKey(connId);
		if (m_rateLimit && m_rateLimit->IsBanned(ipKey))
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_WARN(Auth, "[AUTH] HandleAuth result={} session_id={}", "FAIL", (unsigned long long)0);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "banned", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (m_rateLimit && !m_rateLimit->TryConsumeAuth(ipKey))
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_WARN(Auth, "[AuthRegisterHandler] Auth rate limited (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "rate limited", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		auto parsed = ParseAuthRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_WARN(Auth, "[AuthRegisterHandler] Auth: invalid payload");
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "invalid_payload");
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(ipKey);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid payload", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		std::string login_norm(NormaliseLoginView(parsed->login));
		if (login_norm.empty())
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_WARN(Auth, "[AUTH] HandleAuth result={} session_id={}", "FAIL", (unsigned long long)0);
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "invalid_login");
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(ipKey);
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::INVALID_CREDENTIALS, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (!m_accountStore || !m_sessionManager)
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_ERROR(Auth, "[AuthRegisterHandler] Auth: no store or session manager");
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "unavailable", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		auto opt = m_accountStore->FindByLogin(login_norm);
		if (!opt)
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_WARN(Auth, "[AUTH] HandleAuth result={} session_id={}", "FAIL", (unsigned long long)0);
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "account_not_found");
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(ipKey);
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::ACCOUNT_NOT_FOUND, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (opt->status != AccountStatus::Active)
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_WARN(Auth, "[AUTH] HandleAuth result={} session_id={}", "FAIL", (unsigned long long)0);
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "account_locked");
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::ACCOUNT_LOCKED, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		bool hashOk = engine::auth::Verify(parsed->client_hash, opt->final_hash);
		LOG_INFO(Auth, "[AUTH] HandleAuth hash_ok={}", (int)hashOk);
		if (!hashOk)
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "invalid_credentials");
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(ipKey);
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::INVALID_CREDENTIALS, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_DEBUG(Auth, "[AUTH] HandleAuth result={} session_id={}", "FAIL", (unsigned long long)0);
			return;
		}
		uint64_t session_id = m_sessionManager->CreateSession(opt->account_id);
		if (session_id == 0)
		{
			m_authFailTotal.fetch_add(1, std::memory_order_relaxed);
			LOG_WARN(Auth, "[AUTH] HandleAuth result={} session_id={}", "FAIL", (unsigned long long)0);
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "already_logged_in");
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::ALREADY_LOGGED_IN, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		m_sessionManager->SetState(session_id, SessionState::Authenticated);
		if (m_auditLog) m_auditLog->LogLoginSuccess(ipKey, opt->account_id, session_id);
		if (m_auditLog) m_auditLog->LogSessionCreated(session_id, opt->account_id);
		uint64_t server_time = ServerTimeSecondsSinceEpoch();
		const uint32_t version_gate = 0;
		uint8_t one = 1;
		auto pkt = BuildAuthResponsePacket(one, session_id, server_time, version_gate, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
		if (m_connectionSessionMap)
			m_connectionSessionMap->Add(connId, session_id);
		m_authSuccessTotal.fetch_add(1, std::memory_order_relaxed);
		LOG_INFO(Auth, "[AuthRegisterHandler] Auth success (connId={}, account_id={}, session_id={})", connId, opt->account_id, session_id);
	}
}
