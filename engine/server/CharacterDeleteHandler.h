#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;

	/// Phase 3.9 — Master-side handler for kOpcodeCharacterDeleteRequest.
	/// Verifies the character belongs to the authenticated account, then
	/// soft-deletes by setting `characters.deleted_at = NOW()`. The row
	/// remains in DB (audit, rename freezing, etc.) but is invisible to
	/// CharacterListHandler which filters `WHERE deleted_at IS NULL`.
	///
	/// Security : account_id is resolved server-side via ConnectionSessionMap
	/// + SessionManager. Payload only carries character_id. Cross-account
	/// deletion is impossible because the UPDATE is gated by both id AND
	/// account_id in the WHERE clause.
	class CharacterDeleteHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetConnectionPool(engine::server::db::ConnectionPool* pool);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer* m_server = nullptr;
		SessionManager* m_sessions = nullptr;
		ConnectionSessionMap* m_connMap = nullptr;
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
