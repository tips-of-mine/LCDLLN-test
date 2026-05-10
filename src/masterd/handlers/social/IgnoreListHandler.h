#pragma once
// CMANGOS.25 (Phase 3.25 step 3+4) — IgnoreListHandler : dispatch des opcodes
// IgnoreList (68-73) et appel des methodes correspondantes du
// IgnoreListManager (lui-meme branche sur MysqlIgnoreStore en production).
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 68/70/72 (les requests). Les responses sont emises avec le meme
// requestId / sessionId que la request recue.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 6 dans la reponse type-specific, plus exploitable
// cote UI sociale qu'un BAD_REQUEST generique).

#include <cstdint>
#include <cstddef>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::social
{
	class IgnoreListManager;
}

namespace engine::server
{
	/// Dispatcher IgnoreList. Doit etre configure via Set*() avant tout HandlePacket.
	class IgnoreListHandler
	{
	public:
		/// Branche le manager (autorite runtime des listes ignore + cap a 50).
		/// Le manager wrappe le store (MysqlIgnoreStore en production, in-memory en tests).
		void SetManager(engine::server::social::IgnoreListManager* mgr) { m_mgr = mgr; }
		/// Branche le NetServer pour pouvoir envoyer les reponses.
		void SetServer(NetServer* server) { m_server = server; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes IgnoreList. Dispatch
		/// vers HandleAdd/HandleRemove/HandleList selon l'opcode. Si l'opcode n'est
		/// pas un opcode IgnoreList, ignore silencieusement (filtrage deja fait
		/// cote main_linux).
		///
		/// \param connId         identifiant de connexion TCP (pour Send response).
		/// \param opcode         opcode du paquet entrant (68/70/72).
		/// \param requestId      request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload        pointeur sur le payload (sans header).
		/// \param payloadSize    taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

	private:
		/// Traite IGNORE_ADD_REQUEST : manager->Ignore + retourne IgnoreOpErrorCode.
		void HandleAdd(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite IGNORE_REMOVE_REQUEST : manager->Unignore.
		void HandleRemove(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite IGNORE_LIST_REQUEST : manager->List et renvoie le tableau d'ids.
		void HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		engine::server::social::IgnoreListManager* m_mgr        = nullptr;
		NetServer*                                  m_server     = nullptr;
		SessionManager*                             m_sessionMgr = nullptr;
		ConnectionSessionMap*                       m_connMap    = nullptr;
	};
}
