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
	class ShardRegistry;
	class AnniversaryService;

	/// Phase 4 chat — Master-side handler for kOpcodeCharacterEnterWorldRequest.
	///
	/// Validates that the client's claimed character_id + character_name actually belong
	/// to the authenticated account (DB lookup gated by account_id), then registers the
	/// binding in \ref SessionCharacterMap so subsequent CHAT_SEND_REQUEST can :
	///   - use the character display name as sender (instead of the account login).
	///   - resolve /whisper targets by character name → connId.
	///
	/// TA.3 — Émet aussi un push `kOpcodeMasterToShardAdmitCharacter` au shard sur lequel
	/// vit le perso (résolu via `characters.server_id` + `ShardRegistry::GetShardConnection`)
	/// pour qu'il admette `(account_id, character_id)` dans son `AdmittedCharacterRegistry`.
	/// Sans ce push, le Hello UDP du client serait rejeté (ticket émis avant EnterWorld =
	/// `character_id=0` côté shard).
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
		/// TA.3 — facultatif. Si non configuré, le push d'admission est skip (log WARN unique
		/// au premier EnterWorld). Permet de tourner sans cette fonctionnalité (tests / dev).
		void SetShardRegistry(ShardRegistry* registry);

		/// Anniversaires (spec 2026-07-18) — facultatif. Si configuré, chaque
		/// EnterWorld validé passe par AnniversaryService::OnEnterWorld
		/// (exploits fidélité/naissance + courrier cadeau le jour J) et les
		/// déblocages sont notifiés au client par un ChatRelay « système ».
		void SetAnniversaryService(AnniversaryService* service);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*                          m_server   = nullptr;
		SessionManager*                     m_sessions = nullptr;
		ConnectionSessionMap*               m_connMap  = nullptr;
		SessionCharacterMap*                m_charMap  = nullptr;
		engine::server::db::ConnectionPool* m_pool     = nullptr;
		ShardRegistry*                      m_shardRegistry = nullptr; ///< TA.3 — lookup connId pour push d'admission.
		AnniversaryService*                 m_anniversary = nullptr;   ///< Anniversaires — facultatif (nullptr = off).
	};
}
