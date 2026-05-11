#pragma once
// CMANGOS.33 (Phase 5.33 step 3+4) — LfgHandler : dispatch des opcodes Lfg
// (100/102/104/107) et appel des methodes correspondantes du LfgQueue.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 100/102/104/107.
//
// V1 limitations :
//  - Pas d'etat « proposal » cote master (MatchAccept est juste loggee + Ok).
//  - Pas de timer auto pour TickMatchmaking : doit etre appele manuellement
//    (e.g. depuis un slash command admin ou un scheduler externe). Sub-PR
//    future cablera un timer 5s.
//  - estimatedWaitSec hardcode 60s.
//  - Tracking « ou est inscrit le joueur » fait via une map locale au handler
//    (LfgQueue::Join n'expose pas FindEntry), pour repondre a Leave/Status
//    sans scanner toutes les queues.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized.

#include <cstdint>
#include <cstddef>
#include <unordered_map>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::lfg
{
	class LfgQueue;
}

namespace engine::server
{
	/// Dispatcher Lfg. Doit etre configure via Set*() avant tout HandlePacket.
	class LfgHandler
	{
	public:
		/// Branche la queue in-memory (autorite runtime de l'inscription LFG).
		void SetQueue(engine::server::lfg::LfgQueue* q) { m_queue = q; }
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* server) { m_server = server; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes Lfg. Dispatch
		/// vers HandleQueue/HandleLeave/etc. selon l'opcode. Si l'opcode n'est
		/// pas un opcode Lfg, ignore silencieusement (filtrage deja fait
		/// cote main_linux).
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (100/102/104/107).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// Tick periodique pour faire avancer le matchmaking. A appeler
		/// manuellement en V1 (e.g. depuis un slash command admin ou un
		/// scheduler externe). Sub-PR future cablera un timer 5s.
		///
		/// \param nowMs Timestamp courant en ms (pour l'horodatage des entries
		///              futures ; non utilise en V1).
		///
		/// Effet de bord : pour chaque dungeon avec assez d'entries, forme un
		/// groupe via LfgQueue::TryMatch (qui retire les entries de la queue),
		/// puis envoie un push LFG_MATCH_PROPOSAL_NOTIFICATION a chaque membre
		/// online. Le proposalId est genere via un compteur interne croissant.
		void TickMatchmaking(uint64_t nowMs);

	private:
		/// Traite LFG_QUEUE_REQUEST.
		void HandleQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite LFG_LEAVE_REQUEST.
		void HandleLeave(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite LFG_STATUS_REQUEST.
		void HandleStatus(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite LFG_MATCH_ACCEPT_REQUEST. V1 simplifie : log + reponse Ok via
		/// LfgQueueResponse (le proposal lifecycle est sub-PR future).
		void HandleMatchAccept(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le connId actif d'un account_id (snapshot ConnectionSessionMap +
		/// resolution session->account). Retourne 0 si offline.
		uint32_t FindConnIdForAccount(uint64_t accountId) const;
		/// Recherche le sessionIdHeader actif d'un account_id. Retourne 0 si offline.
		uint64_t FindSessionIdForAccount(uint64_t accountId) const;

		/// Etat interne d'inscription d'un account a la queue. Mirror partiel
		/// de LfgEntry (qui n'expose pas de FindEntry par accountId cote queue).
		struct ActiveEntry
		{
			uint32_t dungeonId  = 0;
			uint8_t  role       = 0;
			uint64_t joinedTsMs = 0;
		};

		engine::server::lfg::LfgQueue*  m_queue       = nullptr;
		NetServer*                       m_server      = nullptr;
		SessionManager*                  m_sessionMgr  = nullptr;
		ConnectionSessionMap*            m_connMap     = nullptr;

		/// Index inverse account -> entry, pour repondre Leave/Status sans
		/// scanner toutes les queues. Cleare a Leave et a TickMatchmaking
		/// quand le membre est forme dans un groupe.
		std::unordered_map<uint64_t, ActiveEntry> m_active;

		/// Compteur monotone pour generer des proposalId. V1 : transient (reset
		/// au reboot). Pas de collision tant qu'un seul master.
		uint64_t m_nextProposalId = 1u;
	};
}
