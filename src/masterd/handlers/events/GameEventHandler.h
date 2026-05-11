#pragma once
// CMANGOS.31 (Phase 5.31 step 3+4) — GameEventHandler : dispatch des opcodes
// GameEvent cote joueur (157/159/161) et appel des methodes correspondantes
// d'un GameEventManager + tracking des subscriptions globales par account.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 157/159/161 (les requests). Les responses 158/160/162 sont
// emises avec le meme requestId / sessionId que la request recue. La push
// notification 163 (StateChange) est emise par le handler pour signaler les
// changements d'etat.
//
// V1 simplifie : l'abonnement est global (pas par event). A chaque
// SubscribeRequest, master compare l'etat actuel des events au dernier
// snapshot diffuse (m_lastBroadcastState) ; pour chaque event dont l'etat
// differe, push StateChangeNotification au nouvel abonne uniquement (pas
// de broadcast cross-subscribers V1) puis met a jour m_lastBroadcastState.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 3) dans la reponse type-specific.
//
// Store in-memory V1 :
//   - 4 events seedees au boot :
//       id=1 "Halloween"               startTsMs=2026-10-15  duration=14d  recur=365d
//       id=2 "Winter Veil"             startTsMs=2026-12-15  duration=21d  recur=365d
//       id=3 "Lunar Festival"          startTsMs=2026-02-01  duration=14d  recur=365d
//       id=4 "Midsummer Fire Festival" startTsMs=2026-06-21  duration=14d  recur=365d
//   - Subscribers : set<account_id> (abonnement global).
//   - lastBroadcastState : map eventId -> dernier state (uint8) push.
//
// V1 limitations :
//   - 4 events hardcodes. Future PR : DB seed via MysqlGameEventStore.
//   - Subscribe = snapshot one-shot pour le nouvel abonne (pas de
//     broadcast cross-subscribers V1).
//   - Subscriptions in-memory (perdues au reboot).
//   - Pas de SyncGameEvents RPC entre master et shardd (master autoritaire V1).

#include "src/masterd/events/GameEventManager.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
	class LunarHandler;
}

namespace engine::server::events
{
	class MysqlGameEventStore;
}

namespace engine::server
{
	/// Dispatcher GameEvent cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class GameEventHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId et
		/// accountId -> sessionId au push.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Branche le LunarHandler pour pouvoir filtrer les events par phase
		/// lunaire. Optionnel (peut etre null) ; si null, le filtre est
		/// ignore (kLunarPhaseAny par defaut sur tous les events).
		///
		/// \param h Pointeur non-owning sur le LunarHandler partage. La duree
		///          de vie doit englober celle du GameEventHandler (cf
		///          main_linux.cpp ou les deux sont des locaux du scope main).
		void SetLunarHandler(engine::server::LunarHandler* h) { m_lunarHandler = h; }

		/// Wave 5 (Phase 5.31b) : branche le store MySQL pour charger les
		/// events depuis la DB plutot que le hardcode. Si null ou DB vide,
		/// SeedV1Events utilise le seed hardcode (5 events).
		///
		/// \param s pointeur non-owning sur le store ; doit etre branche
		///          AVANT SeedV1Events pour effet.
		void SetGameEventStore(engine::server::events::MysqlGameEventStore* s) { m_store = s; }

		/// Initialise le store V1 : enregistre les events hardcodes
		/// (Halloween, Winter Veil, Lunar Festival, Midsummer Fire Festival)
		/// + en Phase 5 Lunar le 5e event "Nuit de la Lune Noire" gate par
		/// les phases lunaires 0/14/15 (kLunarPhaseNoireMask).
		/// Idempotent : appelable a chaque boot.
		void SeedV1Events();

		/// Point d'entree appele par NetServer pour les opcodes GameEvent.
		/// Dispatch vers HandleList / HandleSubscribe / HandleUnsubscribe
		/// selon l'opcode. Si l'opcode n'est pas un opcode GameEvent, ignore
		/// silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (157/159/161).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push GameEventStateChangeNotification
		/// (opcode 163) au client identifie par \p connId. Utilise par le
		/// handler en interne mais accessible egalement depuis l'exterieur
		/// (tests, hooks, future tick periodique).
		///
		/// \param connId     identifiant de connexion TCP cible (0 = no-op).
		/// \param eventId    identifiant de l'event qui change.
		/// \param newState   nouvel etat (0=Inactive, 1=Active).
		/// \param untilTsMs  timestamp absolu de la prochaine bascule
		///                   (Active: when ends ; Inactive: when starts ;
		///                   0 = pas de prochaine bascule connue).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushStateChange(uint32_t connId, uint32_t eventId, uint8_t newState, uint64_t untilTsMs);

	private:
		/// Traite GAME_EVENT_LIST_REQUEST : retourne les 4 events hardcodes V1
		/// + state courant + timestamps complets.
		void HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite GAME_EVENT_SUBSCRIBE_REQUEST : ajoute accountId aux
		/// subscribers globaux. AlreadySubscribed si deja present, sinon OK.
		/// Apres OK, push un snapshot des events dont l'etat actuel differe
		/// du m_lastBroadcastState : un StateChangeNotification par event
		/// au nouvel abonne, puis met a jour m_lastBroadcastState.
		void HandleSubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite GAME_EVENT_UNSUBSCRIBE_REQUEST : retire accountId. OK si
		/// l'etait, NotSubscribed sinon.
		void HandleUnsubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// V1 : 4 events hardcodes au boot + 1 event Phase 5 Lunar
		/// gate par phase lunaire (Lune Noire).
		static constexpr uint32_t kEventHalloween       = 1u;
		static constexpr uint32_t kEventWinterVeil      = 2u;
		static constexpr uint32_t kEventLunarFestival   = 3u;
		static constexpr uint32_t kEventMidsummerFire   = 4u;
		static constexpr uint32_t kEventLuneNoire       = 5u;

		NetServer*                                       m_server       = nullptr;
		SessionManager*                                  m_sessionMgr   = nullptr;
		ConnectionSessionMap*                            m_connMap      = nullptr;
		/// Optionnel : si non-null, sa CurrentPhase() est utilisee pour
		/// filtrer les events via GetStateFiltered. Si null, comportement
		/// inchange (backward compat tests sans LunarHandler).
		LunarHandler*                                    m_lunarHandler = nullptr;

		/// Wave 5 : pointeur non-owning vers le store DB. null = mode no-DB.
		engine::server::events::MysqlGameEventStore*     m_store        = nullptr;

		/// Mutex protegeant m_manager + m_eventNames + m_subscribers
		/// + m_lastBroadcastState + m_seeded.
		mutable std::mutex                               m_mutex;

		/// Manager GameEvent partage : events + states. Modifie uniquement
		/// sous m_mutex.
		engine::server::events::GameEventManager         m_manager;

		/// Noms runtime des events (GameEventManager les stocke deja, mais on
		/// duplique pour un acces rapide cote logging et eviter de relire
		/// la map a chaque appel).
		std::unordered_map<uint32_t, std::string>        m_eventNames;

		/// Abonnement global : set des accountId abonnes aux push.
		std::unordered_set<uint64_t>                     m_subscribers;

		/// Dernier state broadcast par event (pour ne push que sur changement).
		/// Cle = eventId, valeur = uint8 state (0=Inactive, 1=Active).
		std::unordered_map<uint32_t, uint8_t>            m_lastBroadcastState;

		/// True une fois SeedV1Events() appele avec succes.
		bool                                             m_seeded = false;
	};
}
