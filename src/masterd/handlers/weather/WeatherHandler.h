#pragma once
// CMANGOS.42 (Phase 4.42 step 3+4) — WeatherHandler : dispatch des opcodes
// Weather cote joueur (150/152/154) et appel des methodes correspondantes
// d'un WeatherManager + tracking des subscriptions par account et par zone.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 150/152/154 (les requests). Les responses 151/153/155 sont
// emises avec le meme requestId / sessionId que la request recue. La push
// notification 156 (WeatherUpdate) est emise par le handler lui-meme apres
// detection d'un changement d'etat dans une zone.
//
// V1 simplifie : a chaque SubscribeRequest, master appelle ForceReroll sur
// la zone subscribed puis Tick(nowMs, rng). Si l'etat de la zone a change
// (kind ou intensity != snapshot avant Tick), broadcast WeatherUpdateNotification
// a tous les subscribers de cette zone (y compris le nouvel abonne, qui
// verra ainsi un push immediat). Pas de thread Tick periodique en V1.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 3) dans la reponse type-specific.
//
// Store in-memory V1 :
//   - 3 zones meteo seedees au boot :
//       zoneId=1 "Plaines Venteuses" pClear=0.6, pRain=0.3, pStorm=0.1
//       zoneId=2 "Toundra Gelee"     pClear=0.4, pSnow=0.5, pFog=0.1
//       zoneId=3 "Desert Aride"      pClear=0.5, pSandstorm=0.4, pFog=0.1
//   - Subscriptions : map account_id -> set<zoneId>.
//   - Reverse index : map zoneId -> set<account_id> (pour broadcast aux
//     subscribers d'une zone).
//   - Connection mapping : la resolution accountId -> connId est faite via
//     SessionManager + ConnectionSessionMap au moment du push.
//
// V1 limitations :
//   - 3 zones hardcodees. Vraies zones via M40+ futur.
//   - Tick simule a chaque SubscribeRequest (force reroll). Vrai tick
//     periodique via shardd futur.
//   - Subscriptions in-memory (perdues au reboot).
//   - Pas de SyncWeather RPC entre master et shardd (master autoritaire V1).

#include "src/shardd/weather/WeatherManager.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Dispatcher Weather cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class WeatherHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId et
		/// accountId -> sessionId au push.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Initialise le store V1 : enregistre les 3 zones meteo hardcodees
		/// (Stormwind Plains, Frozen Tundra, Tanaris Desert) avec leurs
		/// profiles probabilistes respectifs. Idempotent : appelable a
		/// chaque boot.
		void SeedV1Zones();

		/// Point d'entree appele par NetServer pour les opcodes Weather.
		/// Dispatch vers HandleList / HandleSubscribe / HandleUnsubscribe
		/// selon l'opcode. Si l'opcode n'est pas un opcode Weather, ignore
		/// silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (150/152/154).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push WeatherUpdateNotification (opcode 156)
		/// au client identifie par \p connId. Utilise par le handler en
		/// interne mais accessible egalement depuis l'exterieur (tests, hooks).
		///
		/// \param connId    identifiant de connexion TCP cible (0 = no-op).
		/// \param zoneId    identifiant de la zone qui change.
		/// \param kind      nouveau WeatherKind (uint8 0..5).
		/// \param intensity nouvelle intensite [0..1].
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushWeatherUpdate(uint32_t connId, uint32_t zoneId, uint8_t kind, float intensity);

	private:
		/// Traite WEATHER_LIST_REQUEST : retourne les 3 zones hardcodees V1
		/// + state courant (kind + intensity).
		void HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite WEATHER_SUBSCRIBE_REQUEST : valide zoneId connue, ajoute
		/// aux 2 indices subscriptions (account->zones et zone->accounts).
		/// Force un reroll de la zone et Tick : si state change, broadcast
		/// WeatherUpdateNotification a tous les subscribers de la zone.
		/// Repond avec le state courant apres potentiel changement.
		void HandleSubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite WEATHER_UNSUBSCRIBE_REQUEST : retire des 2 indices.
		/// OK si l'etait, NotSubscribed sinon.
		void HandleUnsubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// Resoudre connId pour un accountId. V1 : iteration sur la map
		/// connId->sessionId puis SessionManager pour matcher l'accountId.
		/// Retourne 0 si non trouve.
		uint32_t FindConnIdForAccount(uint64_t accountId) const;

		/// V1 : 3 zones hardcodees au boot.
		static constexpr uint32_t kZoneStormwindPlains = 1u;
		static constexpr uint32_t kZoneFrozenTundra    = 2u;
		static constexpr uint32_t kZoneTanarisDesert   = 3u;

		NetServer*                                       m_server     = nullptr;
		SessionManager*                                  m_sessionMgr = nullptr;
		ConnectionSessionMap*                            m_connMap    = nullptr;

		/// Mutex protegeant m_manager + m_subscriptions + m_zoneNames + m_rng + m_seeded.
		std::mutex                                       m_mutex;

		/// Manager Weather partage : zones + states + profiles. Modifie
		/// uniquement sous m_mutex.
		engine::server::weather::WeatherManager          m_manager;

		/// Noms runtime des zones (WeatherManager n'en stocke pas).
		/// Cle = zoneId.
		std::unordered_map<uint32_t, std::string>        m_zoneNames;

		/// Subscriptions actives : account_id -> set<zoneId>.
		std::unordered_map<uint64_t, std::unordered_set<uint32_t>> m_subscriptions;

		/// Reverse index : zone_id -> set<account_id> pour broadcast.
		std::unordered_map<uint32_t, std::unordered_set<uint64_t>> m_zoneSubscribers;

		/// RNG seedee a steady_clock pour les Tick. Sous m_mutex.
		std::mt19937                                     m_rng;

		/// True une fois SeedV1Zones() appele avec succes.
		bool                                             m_seeded = false;
	};
}
