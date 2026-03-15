#include "engine/server/AuthRegisterHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/InMemoryAccountStore.h"
#include "engine/server/SessionManager.h"
#include "engine/server/RateLimitAndBan.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/server/ConnectionSessionMap.h"
#include "engine/server/AccountValidation.h"
#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/ErrorPacket.h"
#include "engine/network/NetErrorCode.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/auth/Argon2Hash.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdint>
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
		const std::string ipKey = ConnIdToRateLimitKey(connId);
		if (m_rateLimit && !m_rateLimit->TryConsumeRegister(ipKey))
		{
			LOG_WARN(Auth, "[AuthRegisterHandler] Register rate limited (connId={})", connId);
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
		std::string login_norm(NormaliseLoginView(parsed->login));
		if (login_norm.empty())
		{
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "invalid_login");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::INVALID_LOGIN, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		auto loginErr = ValidateLogin(login_norm);
		if (loginErr != NetErrorCode::OK)
		{
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
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "login_taken");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::LOGIN_ALREADY_TAKEN, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (!email_norm.empty() && m_accountStore->ExistsEmail(email_norm))
		{
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "email_taken");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::INVALID_EMAIL, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		uint64_t account_id = m_accountStore->CreateAccount(login_norm, email_norm, parsed->client_hash);
		if (account_id == 0)
		{
			if (m_auditLog) m_auditLog->LogRegisterFail(ipKey, "create_failed");
			auto pkt = BuildRegisterResponseErrorPacket(NetErrorCode::REGISTRATION_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (m_auditLog) m_auditLog->LogRegisterSuccess(ipKey, account_id);
		uint8_t one = 1;
		auto pkt = BuildRegisterResponsePacket(one, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
		LOG_INFO(Auth, "[AuthRegisterHandler] Register success (connId={}, account_id={})", connId, account_id);
	}

	void AuthRegisterHandler::HandleAuth(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		const std::string ipKey = ConnIdToRateLimitKey(connId);
		if (m_rateLimit && m_rateLimit->IsBanned(ipKey))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "banned", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (m_rateLimit && !m_rateLimit->TryConsumeAuth(ipKey))
		{
			LOG_WARN(Auth, "[AuthRegisterHandler] Auth rate limited (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "rate limited", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		auto parsed = ParseAuthRequestPayload(payload, payloadSize);
		if (!parsed)
		{
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
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "invalid_login");
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(ipKey);
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::INVALID_CREDENTIALS, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (!m_accountStore || !m_sessionManager)
		{
			LOG_ERROR(Auth, "[AuthRegisterHandler] Auth: no store or session manager");
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "unavailable", requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		auto opt = m_accountStore->FindByLogin(login_norm);
		if (!opt)
		{
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "account_not_found");
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(ipKey);
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::ACCOUNT_NOT_FOUND, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (opt->status != AccountStatus::Active)
		{
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "account_locked");
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::ACCOUNT_LOCKED, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		if (!engine::auth::Verify(parsed->client_hash, opt->final_hash))
		{
			if (m_auditLog) m_auditLog->LogLoginFail(ipKey, "invalid_credentials");
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(ipKey);
			auto pkt = BuildAuthResponseErrorPacket(NetErrorCode::INVALID_CREDENTIALS, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		uint64_t session_id = m_sessionManager->CreateSession(opt->account_id);
		if (session_id == 0)
		{
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
		LOG_INFO(Auth, "[AuthRegisterHandler] Auth success (connId={}, account_id={}, session_id={})", connId, opt->account_id, session_id);
	}
}
