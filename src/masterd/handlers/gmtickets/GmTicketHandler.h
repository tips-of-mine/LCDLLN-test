#pragma once
// CMANGOS.32 (Phase 5.32 step 3+4) — GmTicketHandler : dispatch des opcodes
// GmTickets cote joueur (76/78/80) et appel des methodes correspondantes du
// GmTicketSystem (autorite runtime in-memory) + persistance via
// MysqlGmTicketStore (best-effort).
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 76/78/80 (les requests). Les responses sont emises avec le meme
// requestId / sessionId que la request recue.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 6 dans la reponse type-specific, plus exploitable
// cote UI support qu'un BAD_REQUEST generique).

#include <cstddef>
#include <cstdint>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::gmtickets
{
	class GmTicketSystem;
	class MysqlGmTicketStore;
}

namespace engine::server
{
	/// Dispatcher GmTickets cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class GmTicketHandler
	{
	public:
		/// Branche le system in-memory (autorite runtime des tickets).
		void SetSystem(engine::server::gmtickets::GmTicketSystem* sys) { m_sys = sys; }
		/// Branche le store MySQL (persistance audit). Optionnel : si nullptr,
		/// le handler reste fonctionnel mais ne persiste pas en DB (mode no-DB).
		void SetStore(engine::server::gmtickets::MysqlGmTicketStore* store) { m_store = store; }
		/// Branche le NetServer pour pouvoir envoyer les reponses.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes GmTickets. Dispatch
		/// vers HandleOpen/HandleListMine/HandleCancel selon l'opcode. Si l'opcode
		/// n'est pas un opcode GmTickets, ignore silencieusement (filtrage deja
		/// fait cote main_linux).
		///
		/// \param connId         identifiant de connexion TCP (pour Send response).
		/// \param opcode         opcode du paquet entrant (76/78/80).
		/// \param requestId      request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload        pointeur sur le payload (sans header).
		/// \param payloadSize    taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

	private:
		/// Traite GMTICKET_OPEN_REQUEST : valide body, system->Open + store->Insert.
		void HandleOpen(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite GMTICKET_LIST_MINE_REQUEST : itere la queue ouverte du system
		/// et filtre par reporter == accountId.
		void HandleListMine(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite GMTICKET_CANCEL_REQUEST : verifie ownership + state, appelle
		/// system->Cancel + store->Update best-effort.
		void HandleCancel(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		engine::server::gmtickets::GmTicketSystem*       m_sys        = nullptr;
		engine::server::gmtickets::MysqlGmTicketStore*   m_store      = nullptr;
		NetServer*                                        m_server     = nullptr;
		SessionManager*                                   m_sessionMgr = nullptr;
		ConnectionSessionMap*                             m_connMap    = nullptr;
	};
}
