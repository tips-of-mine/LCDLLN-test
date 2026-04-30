#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
	class AccountStore;

	/// Chat MVP — Master-side handler for kOpcodeChatSendRequest.
	///
	/// Resolves the sender's display name from the session (account_id → AccountStore login),
	/// validates the channel + text length, then broadcasts a CHAT_RELAY packet to every
	/// authenticated master session via ConnectionSessionMap::Snapshot().
	///
	/// Limitations volontaires de la v1 :
	///  - Pas de routage par canal (Say/Yell/Zone/Party/Guild/Friends broadcastent tous global) ;
	///    le canal est échoé dans CHAT_RELAY pour préserver l'affichage couleur côté client.
	///  - Whisper (channel=Whisper) : le master renvoie un CHAT_RELAY "Server" notice
	///    "feature not yet available" à l'expéditeur seulement (target lookup nécessite
	///    un mapping display_name → connId qui sera ajouté quand EnterWorld pousse
	///    le perso sélectionné côté master).
	///  - Sender = login. Le character display_name viendra plus tard (Phase 4.x).
	class ChatRelayHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetAccountStore(AccountStore* accounts);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*            m_server   = nullptr;
		SessionManager*       m_sessions = nullptr;
		ConnectionSessionMap* m_connMap  = nullptr;
		AccountStore*         m_accounts = nullptr;
	};
}
