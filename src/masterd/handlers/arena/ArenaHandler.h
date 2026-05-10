#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4) — ArenaHandler : dispatch des opcodes
// Arena cote joueur (120/122/124/127) et appel des methodes correspondantes
// d'un store in-memory de teams + queue + proposals.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 120/122/124/127 (les requests). Les responses 121/123/125/128 sont
// emises avec le meme requestId / sessionId que la request recue. Les push
// notifications 126 (MatchProposal) et 129 (MatchResult) sont emises par le
// handler lui-meme apres traitement (V1 : push proposal immediatement apres
// QueueResponse OK, push result immediatement apres MatchAccept OK).
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 7) dans la reponse type-specific.
//
// Store in-memory V1 : ArenaTeamRegistry seede au premier acces par account
// (3 teams : id=1 size=2 name="LCDLLN A", id=2 size=3 name="LCDLLN B",
// id=3 size=5 name="LCDLLN C", tous a rating=1500). Pas de persistance DB
// en V1 — toutes les progressions sont perdues au reboot, ce qui est acceptable
// pour un V1 (sub-PR future pour la persistance avec migration MysqlArenaStore).
//
// V1 limitations :
//   - Match contre AI Team Alpha fictif (rating 1500). Pairing 2 accounts a venir.
//   - Result win/loss random 50% (V1) ; vraie simulation match a venir.
//   - estimatedWaitSec hardcode 5s (V1).
//   - proposal expiration 30s (V1).

#include "src/shardd/arena/ArenaTeam.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Dispatcher Arena cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class ArenaHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes Arena.
		/// Dispatch vers HandleTeamList / HandleQueue / HandleLeaveQueue /
		/// HandleMatchAccept selon l'opcode. Si l'opcode n'est pas un opcode
		/// Arena, ignore silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (120/122/124/127).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push MatchProposalNotification (opcode 126)
		/// au client identifie par \p connId. Utilise par HandleQueue (V1 : push
		/// immediatement apres QueueResponse OK avec opponent fictif AI).
		///
		/// \param connId            identifiant de connexion TCP cible (0 = no-op).
		/// \param proposalId        id du proposal genere par le master.
		/// \param opponentTeamName  nom de l'equipe adverse (V1 : "AI Team Alpha").
		/// \param opponentRating    rating ELO de l'adversaire (V1 : 1500).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushMatchProposal(uint32_t connId, uint32_t proposalId,
		                        const std::string& opponentTeamName, uint32_t opponentRating);

		/// API publique : pousse une push MatchResultNotification (opcode 129)
		/// au client identifie par \p connId. Utilise par HandleMatchAccept
		/// (V1 : push immediatement apres MatchAccept OK avec result random).
		///
		/// \param connId        identifiant de connexion TCP cible (0 = no-op).
		/// \param win           true si victoire, false si defaite.
		/// \param oldRating     rating avant le match.
		/// \param newRating     rating apres ELO update.
		/// \param opponentName  nom de l'equipe adverse (pour affichage UI).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushMatchResult(uint32_t connId, bool win, uint32_t oldRating,
		                      uint32_t newRating, const std::string& opponentName);

	private:
		/// Traite ARENA_TEAM_LIST_REQUEST : enumere les teams de l'account
		/// (seed au premier acces avec un starter set hardcode V1).
		void HandleTeamList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite ARENA_QUEUE_REQUEST : verifie size valide (2/3/5) et team
		/// existe pour cet account. Si OK, ajoute a la queue puis push
		/// immediatement un proposal contre AI fictive (V1).
		void HandleQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite ARENA_LEAVE_QUEUE_REQUEST : retire de la queue + supprime
		/// les proposals associes. OK si etait en queue, NotInQueue sinon.
		void HandleLeaveQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite ARENA_MATCH_ACCEPT_REQUEST : valide proposalId existe et
		/// appartient a cet account. Si accept=true, push MatchResult avec
		/// ELO update (V1 : 50% random). Si accept=false, supprime proposal
		/// sans push result. ACK Ok dans tous les cas.
		void HandleMatchAccept(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// Seede les 3 teams V1 pour un account si l'entree n'existe pas.
		/// Pas de mutex ici : appele uniquement sous m_mutex deja verrouille.
		void SeedStarterTeamsIfNeeded(uint64_t accountId);

		/// V1 : 3 teams seedees par account (id=1 size=2, id=2 size=3, id=3 size=5).
		static constexpr uint32_t kSeedTeam1Id      = 1u;
		static constexpr uint32_t kSeedTeam2Id      = 2u;
		static constexpr uint32_t kSeedTeam3Id      = 3u;
		static constexpr uint32_t kSeedInitialRating = 1500u;
		/// V1 : estimatedWaitSec retourne dans QueueResponse.
		static constexpr uint32_t kV1EstimatedWaitSec = 5u;
		/// V1 : duree de vie d'un proposal en secondes.
		static constexpr uint32_t kProposalLifetimeSec = 30u;
		/// V1 : opponent fictif pour le pairing AI.
		static constexpr const char* kAiOpponentName = "AI Team Alpha";
		static constexpr uint32_t kAiOpponentRating = 1500u;

		/// Etat d'inscription en queue : un account est dans la queue avec
		/// un teamId + size. queuedAt sert a l'estimation d'attente future.
		struct QueueEntry
		{
			uint32_t                              teamId   = 0;
			uint8_t                               size     = 0;
			std::chrono::steady_clock::time_point queuedAt;
		};

		/// Etat d'un proposal en attente d'accept/reject. accountId pour
		/// verifier l'ownership a MatchAccept. opponentTeamId est 0 en V1
		/// (AI fictive). createdAt sert au check d'expiration (30s V1).
		struct Proposal
		{
			uint64_t                              accountId         = 0;
			uint32_t                              teamId            = 0;
			uint32_t                              opponentTeamId    = 0; ///< 0 = AI fictive (V1).
			std::string                           opponentName;
			uint32_t                              opponentRating    = 0;
			std::chrono::steady_clock::time_point createdAt;
		};

		NetServer*                                       m_server     = nullptr;
		SessionManager*                                  m_sessionMgr = nullptr;
		ConnectionSessionMap*                            m_connMap    = nullptr;

		/// Registry in-memory : seedage par account au premier acces. Le registry
		/// expose Get / AddTeam / RecordMatch / ResetWeekly. V1 : pas d'isolation
		/// par account — toutes les teams partagent le meme namespace numerique
		/// (collision potentielle inter-account ; sub-PR future avec MysqlArenaStore
		/// donnera un teamId globalement unique par auto-increment).
		std::mutex                                       m_mutex;
		engine::server::arena::ArenaTeamRegistry         m_registry;
		/// Track des accounts deja seedes pour eviter de re-seed a chaque acces.
		std::unordered_map<uint64_t, bool>               m_accountSeeded;

		/// Queue active : account_id -> entry. Un seul account ne peut etre que
		/// dans une seule queue (V1 ; pas de queue parallele 2v2/3v3 simultanees).
		std::unordered_map<uint64_t, QueueEntry>         m_queue;

		/// Proposals actifs : proposalId -> Proposal.
		std::unordered_map<uint32_t, Proposal>           m_proposals;

		/// Compteur monotone pour generer des proposalId. V1 : transient (reset
		/// au reboot). Pas de collision tant qu'un seul master.
		uint32_t                                         m_nextProposalId = 1u;

		/// RNG pour le result win/loss V1 (50%). Seede au premier usage (lazy).
		std::mt19937                                     m_rng;
		bool                                             m_rngSeeded = false;
	};
}
