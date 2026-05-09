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
	class SessionCharacterMap;

	/// Phase 4 chat — Master-side handler for kOpcodeCharacterEnterWorldRequest.
	///
	/// Validates that the client's claimed character_id + character_name actually belong
	/// to the authenticated account (DB lookup gated by account_id), then registers the
	/// binding in \ref SessionCharacterMap so subsequent CHAT_SEND_REQUEST can :
	///   - use the character display name as sender (instead of the account login).
	///   - resolve /whisper targets by character name → connId.
	///
	/// Idempotent : the same client may resend if it changes character (logout to
	/// CharacterSelect then re-EnterWorld).
	class CharacterEnterWorldHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetSessionCharacterMap(SessionCharacterMap* charMap);
		void SetConnectionPool(engine::server::db::ConnectionPool* pool);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*                          m_server   = nullptr;
		SessionManager*                     m_sessions = nullptr;
		ConnectionSessionMap*               m_connMap  = nullptr;
		SessionCharacterMap*                m_charMap  = nullptr;
		engine::server::db::ConnectionPool* m_pool     = nullptr;
	};
}
