#pragma once
// CMANGOS.10 (Phase 5 step 3+4) — BattleGroundHandler : dispatch des opcodes
// BG cote joueur (130/132/134/139) et appel des methodes correspondantes
// d'un store in-memory de queue + matches actifs.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 130/132/134/139 (les requests). Les responses 131/133/135 sont
// emises avec le meme requestId / sessionId que la request recue. Les push
// notifications 136 (MatchStart) / 137 (ScoreUpdate) / 138 (MatchEnd) sont
// emises par le handler lui-meme apres traitement.
//
// V1 simplifie : a la queue request, master cree tout de suite un match
// fictif vs AI bot (faction opposee) et envoie immediatement la sequence
// Start -> Score(s) -> End. Pas besoin de Tick(). 139 (LeaveMatch) est
// fire-and-forget : push BgMatchEndNotification (forfait) sans Response paire.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 5) dans la reponse type-specific.
//
// Store in-memory V1 :
//   - 3 BG hardcodes au boot :
//       bgType=1 "Gorge de Feyhin"   teamSize=10 mapName="gorge_feyhin"
//       bgType=2 "Bassin des Ombres" teamSize=15 mapName="bassin_ombres"
//       bgType=3 "Vallee Gelee"      teamSize=40 mapName="vallee_gelee"
//   - Queue : map account_id -> QueueState. Un seul account ne peut etre
//     que dans une seule queue (V1).
//   - Matches actifs : map matchId -> ActiveMatch.
//   - Member-to-match : map account_id -> matchId (index inverse pour
//     LeaveMatch et eviter les doubles).
//
// V1 limitations :
//   - 3 BG hardcodes (Gorge de Feyhin/Bassin des Ombres/Vallee Gelee). Vrais BG via M40+ futur.
//   - Match vs AI bot fictif (account 9999). Vrai matchmaking 2 factions
//     a venir.
//   - Score evolution simulee instantanee (V1) ; vrai gameplay BG via shardd futur.
//   - estimatedWaitSec hardcode 10s (V1).
//   - winnerFaction tirage 50/50 entre joueur et opposite (V1).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::bg_db
{
	class MysqlBattleGroundStore;
}

namespace engine::server
{
	/// Dispatcher BattleGround cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class BattleGroundHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Wave 5 (Phase 5.10b) : branche le store MySQL pour archiver
		/// les matchs joues (table bg_match_history). Optionnel ; si
		/// null ou DB indisponible, les matchs ne sont pas persistes
		/// (les push wire continuent normalement, mode degrade).
		///
		/// \param s pointeur non-owning sur le store. La duree de vie
		///          doit englober celle du handler (cf main_linux).
		void SetMatchHistoryStore(engine::server::bg_db::MysqlBattleGroundStore* s) { m_historyStore = s; }

		/// Point d'entree appele par NetServer pour les opcodes BG.
		/// Dispatch vers HandleList / HandleQueue / HandleLeaveQueue /
		/// HandleLeaveMatch selon l'opcode. Si l'opcode n'est pas un opcode
		/// BG, ignore silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (130/132/134/139).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push BgMatchStartNotification (opcode 136)
		/// au client identifie par \p connId.
		///
		/// \param connId        identifiant de connexion TCP cible (0 = no-op).
		/// \param matchId       identifiant unique du match.
		/// \param bgType        type de BG (1/2/3 V1).
		/// \param mapName       nom de la map (V1 : "gorge_feyhin"/"bassin_ombres"/"vallee_gelee").
		/// \param allianceCount nombre de joueurs cote Alliance.
		/// \param hordeCount    nombre de joueurs cote Horde.
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushMatchStart(uint32_t connId, uint64_t matchId, uint16_t bgType,
		                     const std::string& mapName, uint8_t allianceCount, uint8_t hordeCount);

		/// API publique : pousse une push BgScoreUpdateNotification (opcode 137)
		/// au client identifie par \p connId.
		///
		/// \param connId        identifiant de connexion TCP cible (0 = no-op).
		/// \param matchId       identifiant du match.
		/// \param allianceScore score cumule cote Alliance.
		/// \param hordeScore    score cumule cote Horde.
		/// \param elapsedSec    duree ecoulee depuis le start (secondes).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushScoreUpdate(uint32_t connId, uint64_t matchId,
		                      uint32_t allianceScore, uint32_t hordeScore, uint32_t elapsedSec);

		/// API publique : pousse une push BgMatchEndNotification (opcode 138)
		/// au client identifie par \p connId.
		///
		/// \param connId         identifiant de connexion TCP cible (0 = no-op).
		/// \param matchId        identifiant du match.
		/// \param winnerFaction  0=Alliance, 1=Horde, 2=Draw.
		/// \param allianceScore  score final cote Alliance.
		/// \param hordeScore     score final cote Horde.
		/// \param durationSec    duree totale du match (secondes).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushMatchEnd(uint32_t connId, uint64_t matchId, uint8_t winnerFaction,
		                   uint32_t allianceScore, uint32_t hordeScore, uint32_t durationSec);

	private:
		/// Traite BG_LIST_REQUEST : retourne les 3 BG hardcodes V1.
		void HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite BG_QUEUE_REQUEST : verifie bgType valide (1/2/3) + faction
		/// valide (0/1) + pas deja queued. Si OK, ajoute a la queue, repond
		/// QueueResponse Ok, puis cree immediatement un match V1 (vs AI bot)
		/// et push la sequence Start -> Score(s) -> End.
		void HandleQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite BG_LEAVE_QUEUE_REQUEST : retire de la queue. OK si etait
		/// en queue, NotInQueue sinon.
		void HandleLeaveQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite BG_LEAVE_MATCH_REQUEST : si l'account est en match, push
		/// MatchEndNotification (winnerFaction = opposite, forfait) puis
		/// retire l'index. Pas de Response paire (fire-and-forget V1).
		void HandleLeaveMatch(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// V1 : 3 BG hardcodes au boot. teamSize indicatif (V1 : 1 vrai joueur
		/// + AI bots simules cote opposite).
		static constexpr uint16_t kBgWarsong = 1u;
		static constexpr uint16_t kBgArathi  = 2u;
		static constexpr uint16_t kBgAlterac = 3u;
		/// V1 : estimatedWaitSec hardcode dans QueueResponse.
		static constexpr uint32_t kV1EstimatedWaitSec = 10u;
		/// V1 : queuePosition hardcode dans QueueResponse (1 = match immediat).
		static constexpr uint32_t kV1QueuePosition = 1u;
		/// V1 : account_id fictif de l'AI bot cote opposite.
		static constexpr uint64_t kAiBotAccountId = 9999ull;
		/// V1 : durations simulees pour la sequence Score/End instantanee.
		static constexpr uint32_t kV1ScoreElapsed1 = 5u;
		static constexpr uint32_t kV1ScoreElapsed2 = 10u;
		static constexpr uint32_t kV1ScoreElapsed3 = 15u;
		static constexpr uint32_t kV1MatchDuration = 20u;

		/// Renvoie le BgInfo {bgType, name, teamSize, mapName} pour un
		/// bgType connu. Retourne {0, "", 0, ""} si bgType invalide.
		struct BgInfoEntry
		{
			uint16_t    bgType   = 0;
			std::string name;
			uint8_t     teamSize = 0;
			std::string mapName;
		};
		static BgInfoEntry GetBgInfoStatic(uint16_t bgType);

		/// Verifie que bgType est l'un des BG hardcodes V1 (1/2/3).
		static bool IsValidBgType(uint16_t bgType);

		/// Verifie que faction est dans {0=Alliance, 1=Horde}.
		static bool IsValidFaction(uint8_t faction);

		/// Etat d'inscription en queue : un account est dans la queue avec
		/// un bgType + faction. queuedAt sert a l'estimation d'attente future.
		struct QueueState
		{
			uint16_t                              bgType   = 0;
			uint8_t                               faction  = 0;
			std::chrono::steady_clock::time_point queuedAt;
		};

		/// Etat d'un match actif. allianceScore/hordeScore mis a jour quand
		/// le master push des updates ; durationSec calcule au MatchEnd.
		struct ActiveMatch
		{
			uint16_t                              bgType         = 0;
			std::string                           mapName;
			std::vector<uint64_t>                 alliance;
			std::vector<uint64_t>                 horde;
			uint32_t                              allianceScore  = 0;
			uint32_t                              hordeScore     = 0;
			std::chrono::steady_clock::time_point startedAt;
		};

		NetServer*                                       m_server     = nullptr;
		SessionManager*                                  m_sessionMgr = nullptr;
		ConnectionSessionMap*                            m_connMap    = nullptr;

		/// Wave 5 (Phase 5.10b) : store DB optionnel pour archiver les
		/// matchs joues. Non-owning ; lifetime gere par main_linux.
		engine::server::bg_db::MysqlBattleGroundStore*   m_historyStore = nullptr;

		/// Mutex protegeant queue + matches + accountMatch + nextMatchId + RNG.
		std::mutex                                       m_mutex;

		/// Queue active : account_id -> QueueState. Un seul account ne peut
		/// etre que dans une seule queue (V1).
		std::unordered_map<uint64_t, QueueState>         m_queue;

		/// Matches actifs : matchId -> ActiveMatch.
		std::unordered_map<uint64_t, ActiveMatch>        m_matches;

		/// Index inverse : account_id -> matchId. Permet de trouver le match
		/// d'un account au LeaveMatch ou de bloquer une nouvelle queue tant
		/// que le match precedent n'a pas ete cloture.
		std::unordered_map<uint64_t, uint64_t>           m_accountMatch;

		/// Compteur monotone pour generer des matchId. V1 : transient (reset
		/// au reboot). Pas de collision tant qu'un seul master.
		std::atomic<uint64_t>                            m_nextMatchId{1ull};

		/// RNG pour le winnerFaction V1 (50%). Seede au premier usage (lazy).
		std::mt19937                                     m_rng;
		bool                                             m_rngSeeded = false;
	};
}
