#pragma once
// CMANGOS.24 (Phase 3.24 step 3+4) — ReputationHandler : dispatch des opcodes
// Reputation (95-97) et appel des methodes correspondantes du
// ReputationManager + persistance via MysqlReputationStore.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour
// l'opcode 95 (REPUTATION_LIST_REQUEST).
//
// Le handler expose aussi une API publique ApplyReputationDelta() qu'un autre
// handler (QuestHandler, futurs LootHandler / KillHandler / etc.) peut
// appeler pour declencher un changement de reputation cote serveur. Le
// changement est applique au manager (avec spillover), persiste en DB, et
// pousse au client via REPUTATION_UPDATE_NOTIFICATION (opcode 97) si le
// joueur est connecte. V1 limitations : seule la faction primaire est push,
// les valeurs spillover restent silencieuses (sub-PR future).
//
// Validation session : REPUTATION_LIST_REQUEST exige une session authentifiee.
// Si pas de session, on repond avec error=Unauthorized (code 6).

#include <cstdint>
#include <cstddef>
#include <vector>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::reputation
{
	class ReputationManager;
	class MysqlReputationStore;
}

namespace engine::server
{
	/// Dispatcher Reputation. Doit etre configure via Set*() avant tout HandlePacket.
	class ReputationHandler
	{
	public:
		/// Branche le manager in-memory (autorite runtime des valeurs de rep + spillover).
		void SetManager(engine::server::reputation::ReputationManager* mgr) { m_mgr = mgr; }
		/// Branche le store MySQL (persistance + reload au login). Optionnel : si
		/// nullptr, le handler reste fonctionnel mais ne persiste pas en DB.
		void SetStore(engine::server::reputation::MysqlReputationStore* store) { m_store = store; }
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* server) { m_server = server; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes Reputation.
		/// Dispatch vers HandleListRequest selon l'opcode. Si l'opcode n'est pas
		/// un opcode Reputation, ignore silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (95).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : applique un delta de reputation a un account/faction.
		/// Le manager applique le spillover automatiquement. Le store persiste
		/// best-effort la nouvelle valeur. Si le client est en ligne, un push
		/// REPUTATION_UPDATE_NOTIFICATION est envoye (factionId direct
		/// uniquement — V1 ne push pas le spillover).
		///
		/// \param accountId  compte cible.
		/// \param factionId  faction cible (la rule de spillover, si presente, propage).
		/// \param delta      variation a appliquer (clamp [-42000, +41999] cote manager).
		/// \return true si le manager + store ont applique le changement, false en cas d'erreur cote handler (manager nul, etc.). Le push notification est best-effort (false n'indique pas un echec push).
		bool ApplyReputationDelta(uint64_t accountId, uint32_t factionId, int32_t delta);

	private:
		/// Traite REPUTATION_LIST_REQUEST : enumere les reputations de l'account
		/// depuis le manager (charge depuis le store si l'account n'a aucune
		/// rep en cache et qu'un store est branche).
		void HandleListRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le connId actif d'un account_id (snapshot ConnectionSessionMap +
		/// resolution session->account). Retourne 0 si offline.
		uint32_t FindConnIdForAccount(uint64_t accountId) const;
		/// Recherche le sessionIdHeader actif d'un account_id. Retourne 0 si offline.
		uint64_t FindSessionIdForAccount(uint64_t accountId) const;

		engine::server::reputation::ReputationManager*    m_mgr        = nullptr;
		engine::server::reputation::MysqlReputationStore* m_store      = nullptr;
		NetServer*                                         m_server     = nullptr;
		SessionManager*                                    m_sessionMgr = nullptr;
		ConnectionSessionMap*                              m_connMap    = nullptr;
	};
}
