#pragma once

#include <cstddef>
#include <cstdint>

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
	/// Limitations restantes (Phase 4.5+) :
	///   - Pas de routage par canal pour Say/Yell/Zone/Party/Guild/Friends → tous broadcast global.
	///   - Le canal est juste echoé dans CHAT_RELAY pour préserver la couleur côté client.
	class ChatRelayHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetSessionCharacterMap(SessionCharacterMap* charMap);
		void SetAccountStore(AccountStore* accounts);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*            m_server   = nullptr;
		SessionManager*       m_sessions = nullptr;
		ConnectionSessionMap* m_connMap  = nullptr;
		SessionCharacterMap*  m_charMap  = nullptr;
		AccountStore*         m_accounts = nullptr;
	};
}
