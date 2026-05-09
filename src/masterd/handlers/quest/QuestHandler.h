#pragma once
// CMANGOS.23 (Phase 5.23 step 3+4) — QuestHandler : dispatch des opcodes
// Quest (59-66) et appel des methodes correspondantes du QuestStateTracker
// + persistance via MysqlQuestStateStore.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 59/61/63/65 (les requests). Les responses sont emises avec le meme
// requestId / sessionId que la request recue.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (pas un ErrorPacket — le code 6 dans la reponse
// type-specific est plus exploitable cote UI quete qu'un BAD_REQUEST).

#include <cstdint>
#include <cstddef>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::quests
{
	class QuestStateTracker;
	class MysqlQuestStateStore;
}

namespace engine::server
{
	/// Dispatcher Quest. Doit etre configure via Set*() avant tout HandlePacket.
	class QuestHandler
	{
	public:
		/// Branche le tracker in-memory (autorite runtime des etats Quest).
		void SetTracker(engine::server::quests::QuestStateTracker* t) { m_tracker = t; }
		/// Branche le store MySQL (persistance audit / reload au login).
		/// Optionnel : si nullptr, le handler reste fonctionnel mais ne persiste
		/// pas en DB (mode no-DB).
		void SetStore(engine::server::quests::MysqlQuestStateStore* s) { m_store = s; }
		/// Branche le NetServer pour pouvoir envoyer les reponses.
		void SetServer(NetServer* server) { m_server = server; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes Quest. Dispatch
		/// vers HandleAccept/HandleComplete/etc. selon l'opcode. Si l'opcode
		/// n'est pas un opcode Quest, ignore silencieusement (filtrage deja
		/// fait cote main_linux).
		///
		/// \param connId         identifiant de connexion TCP (pour Send response).
		/// \param opcode         opcode du paquet entrant (59/61/63/65).
		/// \param requestId      request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload        pointeur sur le payload (sans header).
		/// \param payloadSize    taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

	private:
		/// Traite QUEST_ACCEPT_REQUEST : tracker->Accept + store->Upsert si OK.
		void HandleAccept(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite QUEST_COMPLETE_REQUEST.
		void HandleComplete(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite QUEST_REWARD_REQUEST. V1 : la recompense reelle (items/xp) n'est
		/// pas cablee a l'inventaire : on bascule l'etat Completed -> Rewarded
		/// uniquement. NotImplementedYet sera retourne ulterieurement quand un
		/// gating inventaire/level-up sera ajoute.
		void HandleReward(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite QUEST_LIST_REQUEST : itere tracker->ListAll(account) et
		/// renvoie la liste {questId, status} au client.
		void HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		engine::server::quests::QuestStateTracker*    m_tracker    = nullptr;
		engine::server::quests::MysqlQuestStateStore* m_store      = nullptr;
		NetServer*                                     m_server     = nullptr;
		SessionManager*                                m_sessionMgr = nullptr;
		ConnectionSessionMap*                          m_connMap    = nullptr;
	};
}
