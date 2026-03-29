#pragma once

#include <cstdint>

namespace engine::server
{
	class ConnectionSessionMap;
	class InMemoryAccountStore;
	class NetServer;
	class SessionManager;
	struct SmtpConfig;
	class TermsRepository;
}

namespace engine::server
{
	/// Handles CGU opcodes (27–32). Session required except when terms.enforce is off (no-op).
	class TermsHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sm);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetAccountStore(InMemoryAccountStore* store);
		void SetTermsRepository(TermsRepository* repo);
		void SetSmtpConfig(const SmtpConfig* cfg);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*               m_server   = nullptr;
		SessionManager*          m_sessions = nullptr;
		ConnectionSessionMap*    m_connMap  = nullptr;
		InMemoryAccountStore*    m_accounts = nullptr;
		TermsRepository*         m_repo     = nullptr;
		const SmtpConfig*        m_smtp     = nullptr;
	};
}
