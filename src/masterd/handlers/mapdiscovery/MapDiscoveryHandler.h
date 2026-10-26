#pragma once
// MapDiscoveryHandler — handler master de la carte à découvrir :
//   - kOpcodeMapDiscoveryRequest (208) → page de masques, réponse 209 ;
//   - kOpcodeMapDiscoveryUpdate (210) → ignoré depuis ETAPE2.5 : la carte est
//     écrite par le shard propriétaire (position autoritaire).
// Le personnage est celui que la connexion a déclaré à l'EnterWorld
// (SessionCharacterMap) : un client ne peut lire ni écrire que la carte du
// personnage qu'il joue.

#include <cstddef>
#include <cstdint>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
	class SessionCharacterMap;
	class MysqlMapDiscoveryStore;

	class MapDiscoveryHandler
	{
	public:
		void SetServer(NetServer* server)                         { m_server = server; }
		void SetSessionManager(SessionManager* sessions)          { m_sessions = sessions; }
		void SetConnectionSessionMap(ConnectionSessionMap* map)   { m_connMap = map; }
		void SetSessionCharacterMap(SessionCharacterMap* map)     { m_charMap = map; }
		void SetStore(MysqlMapDiscoveryStore* store)              { m_store = store; }

		/// Route 208 (lecture paginée) et 210 (fusion). Erreurs wire de 209 :
		/// Unauthorized (pas de session / pas de personnage en jeu), Unavailable
		/// (no-DB ou erreur DB). Une mise à jour 210 refusée est seulement journalisée.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
			uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*              m_server   = nullptr;
		SessionManager*         m_sessions = nullptr;
		ConnectionSessionMap*   m_connMap  = nullptr;
		SessionCharacterMap*    m_charMap  = nullptr;
		MysqlMapDiscoveryStore* m_store    = nullptr;
	};
}
