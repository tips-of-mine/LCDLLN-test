#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <cstdint>
#include <string_view>

namespace engine::server
{
	class NetServer;
	class InMemoryAccountStore;
	class SessionManager;
	class RateLimitAndBan;
	class SecurityAuditLog;

	/// Handles AUTH_REQUEST and REGISTER_REQUEST opcodes: validate, store/session, rate-limit, audit, send response.
	/// Uses request_id and session_id from packet header for responses. Single-threaded worker use.
	class AuthRegisterHandler
	{
	public:
		AuthRegisterHandler() = default;

		/// Set dependencies. Call before HandlePacket. Null rateLimit or auditLog skips that step.
		void SetServer(NetServer* server);
		void SetAccountStore(InMemoryAccountStore* store);
		void SetSessionManager(SessionManager* sessionManager);
		void SetRateLimitAndBan(RateLimitAndBan* rateLimit);
		void SetSecurityAuditLog(SecurityAuditLog* auditLog);

		/// Handle one packet. Dispatches by opcode; sends response via NetServer::Send. Ignores unknown opcodes.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		void HandleRegister(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);
		void HandleAuth(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

		NetServer* m_server = nullptr;
		InMemoryAccountStore* m_accountStore = nullptr;
		SessionManager* m_sessionManager = nullptr;
		RateLimitAndBan* m_rateLimit = nullptr;
		SecurityAuditLog* m_auditLog = nullptr;
	};

}
