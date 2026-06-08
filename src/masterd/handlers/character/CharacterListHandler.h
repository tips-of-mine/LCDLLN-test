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
	class WorldClockHandler;

	/// Phase 1 — Master-side handler for kOpcodeCharacterListRequest.
	/// Resolves connId → sessionId → accountId, queries the master DB for the
	/// account's characters on the requested server_id (filtered, soft-delete
	/// aware), and replies with kOpcodeCharacterListResponse.
	///
	/// The request payload carries the server_id of the shard the user just
	/// authenticated on (provided by the client after TICKET_ACCEPTED). All
	/// other context (account_id) is derived server-side from the session
	/// header — never from the payload — so a malicious client cannot list
	/// another account's characters.
	class CharacterListHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetConnectionPool(engine::server::db::ConnectionPool* pool);

		/// Branche le WorldClockHandler pour embarquer l'horloge dans la réponse
		/// liste perso (piggyback opcode 40). Optionnel : si nullptr, aucune
		/// horloge n'est jointe (hasWorldClock = 0).
		void SetWorldClockHandler(WorldClockHandler* wc);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer* m_server = nullptr;
		SessionManager* m_sessions = nullptr;
		ConnectionSessionMap* m_connMap = nullptr;
		engine::server::db::ConnectionPool* m_pool = nullptr;
		WorldClockHandler* m_worldClock = nullptr;
	};
}
