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
	class AccountStore;

	/// Master-side handler for kOpcodeChatSendRequest.
	///
	/// Phase 4 :
	///   - Sender display name = character_name (via SessionCharacterMap), fallback login.
	///   - Whisper (channel=Whisper) : résolu via SessionCharacterMap par character_name
	///     normalisé. Si target offline → CHAT_RELAY "Server" notice à l'expéditeur seul.
	///     Si target online → CHAT_RELAY (channel=Whisper) envoyé à target ET sender.
	///
	/// Phase 5.1 :
	///   - Guild (channel=Guild) : SQL `guild_members` → set d'account_id de la guilde du
	///     sender → mapping account_id → connId via SessionManager + ConnectionSessionMap →
	///     CHAT_RELAY uniquement aux membres en ligne. Si sender pas dans une guilde, notice
	///     "Server" "You are not in a guild" à l'expéditeur seul.
	///
	/// Limitations restantes (Phase 5.2+) :
	///   - /p (party) et /zone : pas de routage, broadcast global. Party vit en RAM côté
	///     shard (PartySystem.cpp), pas accessible au master ; zone idem.
	///   - Le canal est juste echoé dans CHAT_RELAY pour préserver la couleur côté client.
	class ChatRelayHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetSessionCharacterMap(SessionCharacterMap* charMap);
		void SetAccountStore(AccountStore* accounts);
		void SetConnectionPool(engine::server::db::ConnectionPool* pool);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*                          m_server   = nullptr;
		SessionManager*                     m_sessions = nullptr;
		ConnectionSessionMap*               m_connMap  = nullptr;
		SessionCharacterMap*                m_charMap  = nullptr;
		AccountStore*                       m_accounts = nullptr;
		engine::server::db::ConnectionPool* m_pool     = nullptr;
	};
}
