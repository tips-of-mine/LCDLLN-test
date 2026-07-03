#pragma once
// CMANGOS.30 (Phase 5.30 step 3+4) — CinematicHandler : dispatch des opcodes
// Cinematics cote master (109/111) + helper public PushCinematic pour
// envoyer un push notification 108 a un client donne.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 109/111 (les requests cote client). Les opcodes 108/110/112 sont
// envoyes par le master ; le client les recoit et dispatch dans son push
// handler (cf. Engine::SetMasterPushHandler).
//
// V1 limitations :
//   - Pas de tracking server-side d'active cinematic : Ack repond toujours
//     Ok, Skip repond toujours Ok + allowed=true.
//   - Pas de catalog "non-skippable sequences" : skip toujours autorise.
//   - PushCinematic resout le connId par scan ConnectionSessionMap (helper
//     duplicate du LfgHandler).
//
// Validation session : chaque opcode entrant exige une session authentifiee.
// Le handler resout connId -> sessionId via ConnectionSessionMap, puis
// sessionId -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized.

#include <cstddef>
#include <cstdint>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::cinematics { class ICinematicStore; }

namespace engine::server
{
	/// Dispatcher Cinematics. Doit etre configure via Set*() avant tout
	/// HandlePacket / PushCinematic.
	class CinematicHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* server) { m_server = server; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }
		/// Branche le store cinematic_seen (Wave 11). Si null, le handler
		/// ne persiste pas le "seen" mais reste fonctionnel (V1 fallback).
		/// Le store doit survivre au handler.
		void SetCinematicStore(engine::server::cinematics::ICinematicStore* s) { m_store = s; }

		/// True si l'account a deja vu la sequence selon le store branche.
		/// Retourne false si le store est null (mode no-store = jamais vue).
		/// Pratique pour les callers PushCinematic qui veulent gater les
		/// replays (ex: skip intro a la 2e session).
		///
		/// \param accountId  account interroge.
		/// \param sequenceId id de la cinematic.
		bool HasSeen(uint64_t accountId, uint32_t sequenceId) const;

		/// Point d'entree appele par NetServer pour les opcodes Cinematics
		/// entrants (109 Ack, 111 Skip). Si l'opcode n'est pas un opcode
		/// Cinematics, ignore silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (109 ou 111).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique pour qu'un autre handler (ex: LootHandler, GMCommand,
		/// etc.) push une cinematic au client d'un account donne.
		///
		/// \param accountId  account cible (le client en ligne sur le master).
		/// \param sequenceId id de la sequence a jouer (mappe vers seq<id>.json
		///                   cote client).
		/// \param reason     cf. CinematicReason (0=ZoneEnter, 1=QuestComplete,
		///                   2=Intro, 3=Other). Logue cote master, pas
		///                   d'effet runtime.
		/// \return true si la push notification a ete enqueue ; false si le
		///         client est offline (pas de connId resoluble) ou si le
		///         handler n'est pas wire.
		///
		/// Effet de bord : envoie un paquet 108 (push, requestId=0) sur la
		/// connexion master du client.
		bool PushCinematic(uint64_t accountId, uint32_t sequenceId, uint8_t reason);

	private:
		/// Traite CINEMATIC_ACK_REQUEST (109). V1 : log info + send response Ok.
		void HandleAckRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite CINEMATIC_SKIP_REQUEST (111). V1 : toujours autoriser le skip
		/// (allowed=true). Future PR pourra introduire un catalog
		/// "non-skippable" sequences.
		void HandleSkipRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le connId actif d'un account_id (snapshot ConnectionSessionMap +
		/// resolution session->account). Retourne 0 si offline.
		///
		/// \note Helper duplique de LfgHandler (et autres) ; sera factorise
		/// dans une sub-PR future.
		uint32_t FindConnIdForAccount(uint64_t accountId) const;

		/// Recherche le sessionIdHeader actif d'un account_id. Retourne 0 si offline.
		uint64_t FindSessionIdForAccount(uint64_t accountId) const;

		NetServer*            m_server     = nullptr;
		SessionManager*       m_sessionMgr = nullptr;
		ConnectionSessionMap* m_connMap    = nullptr;
		engine::server::cinematics::ICinematicStore* m_store = nullptr;
	};
}
