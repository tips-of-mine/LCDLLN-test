#pragma once
// LunarHandler : etat lunaire authoritative master + push broadcast sur
// changement de phase. Calcul stateless via LunarCalendar (deterministe
// depuis epoch). Tick periodique (5min) verifie si la phase courante a
// change depuis le dernier broadcast et push aux clients connectes.
//
// Le handler est instancie dans main_linux.cpp au boot, cable via
// SetServer/SetSessionManager/SetConnectionSessionMap, puis enregistre
// dans le packetHandler du NetServer pour l'opcode 192. La response 193
// est emise avec le meme requestId/sessionId que la request recue. Le
// push 194 est broadcast a tous les clients connectes (pas de subscribe
// explicite : la lune est globale et permanente).
//
// Cycle par defaut : 14 jours reels (14*24*3600*1000 = 1209600000 ms),
// epoch 2026-01-01 00:00:00 UTC = 1767225600000 ms.
//
// V1 limitations :
//   - Pas de hook event lune <-> GameEvents (CurrentPhase expose pour future PR).
//   - Pas de SyncLunar RPC entre master et shardd : master autoritaire,
//     le shardd peut recalculer la phase via LunarCalendar (header-only).
//   - Pas de override admin (parametre fixe au boot via SetCycleParams).

#include "src/shardd/world/LunarCalendar.h"

#include <cstdint>
#include <mutex>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
	class WorldClockHandler;
}

namespace engine::server
{
	/// Dispatcher Lunar cote master + tick periodique 5 min.
	/// Doit etre configure via Set*() avant tout HandlePacket / Tick.
	class LunarHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId (validation auth).
		void SetSessionManager(SessionManager* mgr) { m_sessionMgr = mgr; }
		/// Branche la map connId -> sessionId pour valider les requests + iterer pour le push.
		void SetConnectionSessionMap(ConnectionSessionMap* m) { m_connMap = m; }
		/// Branche l'horloge monde : si non-null, la phase lunaire derive du temps
		/// de JEU (GameSeconds + lunarPeriodGameSec) au lieu du temps reel, ce qui
		/// unifie la cadence lunaire avec la cadence jour/nuit (timeScale, pause,
		/// /settime s'appliquent aussi a la lune). Si null, fallback temps reel
		/// (comportement d'origine via m_cycleStartMs / m_cycleDurationMs).
		void SetWorldClock(WorldClockHandler* wc) { m_worldClock = wc; }

		/// Configure les parametres du cycle. Appele une fois au boot avant le
		/// premier Tick. Modifie uniquement les parametres ; ne reset pas
		/// m_lastBroadcastPhase (le prochain Tick fera le diff comme d'habitude).
		/// \param cycleStartMs    Timestamp Unix ms du debut de cycle de reference.
		/// \param cycleDurationMs Duree d'un cycle complet en ms.
		void SetCycleParams(uint64_t cycleStartMs, uint64_t cycleDurationMs);

		/// Dispatch packet : opcode 192 (LunarStateRequest) -> response 193.
		/// Si l'opcode n'est pas Lunar, ignore silencieusement.
		///
		/// \param connId           identifiant de connexion TCP (pour Send response).
		/// \param opcode           opcode du paquet entrant (192).
		/// \param requestId        request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader  session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload          pointeur sur le payload (sans header).
		/// \param payloadSize      taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize);

		/// Tick periodique (typiquement appele toutes les 5 min). Compare la
		/// phase courante avec la derniere broadcastee ; si different, push
		/// 194 a tous les clients connectes (Snapshot ConnectionSessionMap).
		///
		/// \param realNowMs Timestamp Unix courant en ms.
		void Tick(uint64_t realNowMs);

		/// Phase courante (pour integration future avec GameEvents).
		/// Lecture sous mutex pour coherence avec SetCycleParams.
		uint8_t CurrentPhase() const;

	private:
		/// Calcule la phase lunaire courante de maniere centralisee. Unifie la
		/// source : si l'horloge monde est branchee (m_worldClock != null), la
		/// phase derive du temps de JEU (GameSeconds + lunarPeriodGameSec), sinon
		/// fallback temps reel (m_cycleStartMs / m_cycleDurationMs, comportement
		/// d'origine). NE prend PAS m_mutex (lit m_worldClock/m_cycleStartMs/
		/// m_cycleDurationMs ; les appelants qui mutent ces champs verrouillent
		/// deja). \param realNowMs Timestamp Unix courant en ms (entree du calcul).
		engine::server::world::LunarPhaseInfo ComputePhaseNow(uint64_t realNowMs) const;

		/// Traite LUNAR_STATE_REQUEST : valide session, calcule phase courante,
		/// envoie une reponse 193 avec la phase + cycle params.
		void HandleStateRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader);

		/// Push une LUNAR_PHASE_CHANGE_NOTIFICATION (opcode 194, push) a tous les
		/// clients connectes (snapshot de ConnectionSessionMap).
		void PushPhaseChangeBroadcast(uint8_t newPhase, float newIllumination, uint64_t nextChangeTsMs);

		NetServer*            m_server     = nullptr;
		SessionManager*       m_sessionMgr = nullptr;
		ConnectionSessionMap* m_connMap    = nullptr;
		/// Horloge monde optionnelle. Si non-null, ComputePhaseNow derive la
		/// phase du temps de jeu ; sinon fallback temps reel. Non possede.
		WorldClockHandler*    m_worldClock = nullptr;

		/// Mutex protegeant m_cycleStartMs / m_cycleDurationMs / m_lastBroadcastPhase.
		mutable std::mutex m_mutex;
		/// Epoch 2026-01-01 00:00:00 UTC = 1767225600000 ms (cycle de reference).
		uint64_t           m_cycleStartMs    = 1767225600000ull;
		/// Duree par defaut : 14 jours reels (cf. kDefaultLunarCycleMs).
		uint64_t           m_cycleDurationMs = engine::server::world::kDefaultLunarCycleMs;
		/// Sentinel 0xFFu : force le premier broadcast au premier Tick (la phase
		/// courante sera != 0xFFu, donc differente de m_lastBroadcastPhase).
		uint8_t            m_lastBroadcastPhase = 0xFFu;
	};
}
