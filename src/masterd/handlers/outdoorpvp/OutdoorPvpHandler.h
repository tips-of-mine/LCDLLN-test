#pragma once
// CMANGOS.36 (Phase 5.36 step 3+4) — OutdoorPvpHandler : dispatch des opcodes
// OutdoorPvP cote joueur (140/142/144/146) et appel des methodes correspondantes
// d'un OutdoorPvPManager + tracking des subscriptions par account.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 140/142/144/146 (les requests). Les responses 141/143/145/147 sont
// emises avec le meme requestId / sessionId que la request recue. Les push
// notifications 148 (CaptureProgress) / 149 (CaptureCompleted) sont emises
// par le handler lui-meme apres traitement.
//
// V1 simplifie : a la CaptureStartRequest, master simule la capture
// instantanee : push 4 progress (capturePct = 25/50/75/100) puis
// TickCapture(zid, oid, 100) qui transitionne l'owner et incremente le score,
// puis push CaptureCompletedNotification au connectionId qui a demarre.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 5) dans la reponse type-specific.
//
// Store in-memory V1 :
//   - 2 zones contestees au boot :
//       zoneId=1 "Hellfire Peninsula"   3 objectifs (10, 11, 12)
//       zoneId=2 "Eastern Plaguelands"  4 objectifs (20, 21, 22, 23)
//     Tous les objectifs commencent owner=0xFF (neutre), capturePct=0.
//   - Subscriptions : map account_id -> set<zoneId>.
//   - Pas de tracking des progressions actives (V1 capture = instantanee).
//
// V1 limitations :
//   - 2 zones hardcodees. Vraies zones via M40+ futur.
//   - Capture simulee instantanement (V1) ; vrai gameplay via shardd futur.
//   - Subscriptions in-memory (perdues au reboot).
//   - Pas de SyncOutdoorPvp RPC entre master et shardd (master autoritaire V1).
//   - Push CaptureProgress/Completed envoyes uniquement au connectionId
//     initiateur de la capture (pas broadcast aux subscribers V1).

#include "src/shardd/outdoorpvp/OutdoorPvPManager.h"

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
}

namespace engine::server::outdoorpvp_db
{
	class MysqlOutdoorPvpStore;
}

namespace engine::server
{
	/// Dispatcher OutdoorPvP cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class OutdoorPvpHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Wave 5 (Phase 5.36b) : branche le store MySQL pour la persistance
		/// des objectifs (owner, capturePct, capturingBy) et scores. Si null
		/// (mode no-DB), seed in-memory au reboot et perte des captures.
		void SetOutdoorPvpStore(engine::server::outdoorpvp_db::MysqlOutdoorPvpStore* s) { m_store = s; }

		/// Initialise le store V1 : enregistre les 2 zones contestees hardcodees
		/// (Hellfire Peninsula 3 objectifs, Eastern Plaguelands 4 objectifs).
		/// Idempotent : appelable a chaque boot.
		void SeedV1Zones();

		/// Point d'entree appele par NetServer pour les opcodes OutdoorPvP.
		/// Dispatch vers HandleZoneList / HandleSubscribe / HandleUnsubscribe /
		/// HandleCaptureStart selon l'opcode. Si l'opcode n'est pas un opcode
		/// OutdoorPvP, ignore silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (140/142/144/146).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push CaptureProgressNotification (opcode 148)
		/// au client identifie par \p connId.
		///
		/// \param connId       identifiant de connexion TCP cible (0 = no-op).
		/// \param zoneId       identifiant de la zone.
		/// \param objectiveId  identifiant de l'objectif capture.
		/// \param capturePct   progression (0..100).
		/// \param capturingBy  faction qui progresse (0=Alliance, 1=Horde, 0xFF si aucune).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushCaptureProgress(uint32_t connId, uint32_t zoneId, uint32_t objectiveId,
		                          uint32_t capturePct, uint8_t capturingBy);

		/// API publique : pousse une push CaptureCompletedNotification (opcode 149)
		/// au client identifie par \p connId.
		///
		/// \param connId         identifiant de connexion TCP cible (0 = no-op).
		/// \param zoneId         identifiant de la zone.
		/// \param objectiveId    identifiant de l'objectif capture.
		/// \param newOwner       nouvelle faction propriétaire (0=Alliance, 1=Horde, 0xFF reset).
		/// \param allianceScore  nouveau score Alliance dans la zone.
		/// \param hordeScore     nouveau score Horde dans la zone.
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushCaptureCompleted(uint32_t connId, uint32_t zoneId, uint32_t objectiveId,
		                           uint8_t newOwner, uint32_t allianceScore, uint32_t hordeScore);

	private:
		/// Traite OUTDOOR_PVP_ZONE_LIST_REQUEST : retourne les 2 zones hardcodees
		/// V1 + objectifs + scores actuels.
		void HandleZoneList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite OUTDOOR_PVP_SUBSCRIBE_REQUEST : valide zoneId connue, ajoute a
		/// la map des subscriptions de l'account.
		void HandleSubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite OUTDOOR_PVP_UNSUBSCRIBE_REQUEST : retire la subscription. OK
		/// si l'etait, NotSubscribed sinon.
		void HandleUnsubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite OUTDOOR_PVP_CAPTURE_START_REQUEST : valide zone/objectif/faction,
		/// appelle BeginCapture puis simule la capture instantanee (push 4
		/// progress + 1 completed avec scores updated).
		void HandleCaptureStart(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// V1 : 2 zones hardcodees au boot.
		static constexpr uint32_t kZoneHellfire        = 1u;
		static constexpr uint32_t kZoneEasternPlaguelands = 2u;

		/// V1 : valeurs de progression simulee envoyees a chaque CaptureStart.
		static constexpr uint32_t kV1ProgressStep1 = 25u;
		static constexpr uint32_t kV1ProgressStep2 = 50u;
		static constexpr uint32_t kV1ProgressStep3 = 75u;
		static constexpr uint32_t kV1ProgressStep4 = 100u;

		NetServer*                                       m_server     = nullptr;
		SessionManager*                                  m_sessionMgr = nullptr;
		ConnectionSessionMap*                            m_connMap    = nullptr;

		/// Wave 5 : store DB. null = mode no-DB.
		engine::server::outdoorpvp_db::MysqlOutdoorPvpStore* m_store  = nullptr;

		/// Mutex protegeant m_manager + m_subscriptions + m_zoneNames + seeded.
		std::mutex                                       m_mutex;

		/// Manager OutdoorPvP partage : zones + objectifs + scores. Modifie
		/// uniquement sous m_mutex.
		engine::server::outdoorpvp::OutdoorPvPManager    m_manager;

		/// Noms runtime des zones (Zone struct ne contient pas de name V1).
		/// Cle = zoneId.
		std::unordered_map<uint32_t, std::string>        m_zoneNames;

		/// Subscriptions actives : account_id -> set<zoneId>.
		std::unordered_map<uint64_t, std::unordered_set<uint32_t>> m_subscriptions;

		/// True une fois SeedV1Zones() appele avec succes.
		bool                                             m_seeded = false;
	};
}
