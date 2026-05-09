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

	/// Phase 3.6.5 — Master-side handler for kOpcodeCharacterSavePositionRequest.
	/// Persists the current camera position of the active character into
	/// `characters.spawn_x/y/z/yaw_deg/pitch_deg`. The next time the user logs in
	/// and reaches CharacterSelect, those columns are read back via Phase 3.6
	/// (`CharacterListEntry::spawn_*`) and applied as the spawn point.
	///
	/// Security : `account_id` is resolved server-side from the session header
	/// (ConnectionSessionMap + SessionManager). The UPDATE WHERE clause is gated
	/// by both `id` AND `account_id` so a malicious client cannot save the
	/// position of someone else's character. Returns NOT_FOUND if the row does
	/// not exist or is not owned (no oracle of existence).
	///
	/// Reliability note : in the current architecture the client is the source
	/// of truth for the position (camera state). A compromised client can lie.
	/// A future Phase will move authoritative position to the shard (via UDP
	/// gameplay state) and have the shard push to master through an internal
	/// opcode — see Phase 3.6.6 (planned).
	class CharacterSavePositionHandler
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
