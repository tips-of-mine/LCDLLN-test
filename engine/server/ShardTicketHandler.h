#pragma once

#include <cstdint>
#include <string>

namespace engine::server
{
	class ConnectionSessionMap;
	class InMemoryAccountStore;
	class NetServer;
	class ShardRegistry;
	class SessionManager;
	class TermsRepository;
}

namespace engine::server
{
	/// Handles REQUEST_SHARD_TICKET on the Master: issues a short-lived ticket (HMAC v1) for client→shard connection (M22.4).
	class ShardTicketHandler
	{
	public:
		ShardTicketHandler() = default;

		void SetServer(NetServer* server);
		void SetShardRegistry(ShardRegistry* registry);
		void SetSessionManager(SessionManager* sessionManager);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		/// Optional: block ticket if e-mail not verified (M33.2) or CGU pending.
		void SetAccountStore(InMemoryAccountStore* store);
		void SetTermsRepository(TermsRepository* repo);

		/// Secret for HMAC (shared with shards). Empty = reject all requests.
		void SetSecret(std::string secret);
		/// Ticket validity in seconds (e.g. 30–60). Default 60.
		void SetValiditySec(int sec) { m_validity_sec = (sec > 0) ? sec : 60; }

		/// Handles REQUEST_SHARD_TICKET only. Ignores other opcodes.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer* m_server = nullptr;
		ShardRegistry* m_registry = nullptr;
		SessionManager* m_sessionManager = nullptr;
		ConnectionSessionMap* m_connSessionMap = nullptr;
		InMemoryAccountStore* m_accountStore   = nullptr;
		TermsRepository*      m_termsRepository = nullptr;
		std::string m_secret;
		int m_validity_sec = 60;
	};
}
