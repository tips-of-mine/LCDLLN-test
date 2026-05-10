#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4) — Wire payloads pour les opcodes Arena
// (120-129). 4 paires Request/Response + 2 push :
//   - TeamList                        (120/121)
//   - Queue                           (122/123)
//   - LeaveQueue                      (124/125)
//   - MatchProposalNotification       (126 push) : push Master to Client
//     annonce qu'un match a ete forme.
//   - MatchAccept                     (127/128)  : ACK accept/reject.
//   - MatchResultNotification         (129 push) : push Master to Client
//     annonce le resultat d'un match (win/loss + ELO update).
//
// Le ArenaTeamRegistry est garde cote master uniquement en V1 (pas de DB) :
// au reboot, les teams seedees + queue + proposals sont perdus. Acceptable V1.
//
// Format wire : ByteReader/ByteWriter little-endian. Toutes les strings
// passent par WriteString/ReadString (uint16 length + UTF-8 bytes), et
// toutes les arrays par WriteArrayCount/ReadArrayCount (uint16 count).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Arena.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Arena. 0 = OK.
	enum class ArenaErrorCode : uint8_t
	{
		Ok               = 0,
		AlreadyQueued    = 1, ///< Le compte est deja inscrit dans une queue arena.
		TeamNotFound     = 2, ///< teamId inconnu pour ce compte.
		InvalidSize      = 3, ///< size hors {2, 3, 5}.
		NotInQueue       = 4, ///< Pas inscrit dans une queue arena (LeaveQueue).
		ProposalExpired  = 5, ///< Le proposal a expire (timeout 30s) — MatchAccept.
		UnknownProposal  = 6, ///< proposalId inconnu cote master — MatchAccept.
		Unauthorized     = 7, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// ARENA_TEAM_LIST — Client to Master : liste des arena teams du compte.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct ArenaTeamListRequestPayload
	{
		// (vide)
	};

	/// Resume d'une arena team pour le wire (mirror partiel d'arena::ArenaTeam).
	/// Wire format :
	///   uint32 teamId
	///   uint8  size           (2, 3 ou 5)
	///   string name           (uint16 length + UTF-8)
	///   uint32 rating
	///   uint32 weeklyGames
	///   uint32 weeklyWins
	struct ArenaTeamSummary
	{
		uint32_t    teamId       = 0;
		uint8_t     size         = 2; ///< 2 / 3 / 5.
		std::string name;
		uint32_t    rating       = 1500;
		uint32_t    weeklyGames  = 0;
		uint32_t    weeklyWins   = 0;
	};

	/// Wire format :
	///   uint8  error           (cf. ArenaErrorCode)
	///   uint16 count            (si error == 0)
	///   <count> ArenaTeamSummary
	struct ArenaTeamListResponsePayload
	{
		uint8_t                       error = 0;
		std::vector<ArenaTeamSummary> teams;
	};

	// =========================================================================
	// ARENA_QUEUE — Client to Master : inscription a la queue arena.
	// =========================================================================

	/// Wire format :
	///   uint32 teamId
	///   uint8  size            (2, 3 ou 5)
	struct ArenaQueueRequestPayload
	{
		uint32_t teamId = 0;
		uint8_t  size   = 0;
	};

	/// Wire format :
	///   uint8  error             (cf. ArenaErrorCode)
	///   uint32 estimatedWaitSec  (si error == 0)
	struct ArenaQueueResponsePayload
	{
		uint8_t  error            = 0;
		uint32_t estimatedWaitSec = 0;
	};

	// =========================================================================
	// ARENA_LEAVE_QUEUE — Client to Master : quitte la queue arena.
	// =========================================================================

	/// Wire format : (vide). L'account derive de la session cote master.
	struct ArenaLeaveQueueRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8 error
	struct ArenaLeaveQueueResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// ARENA_MATCH_PROPOSAL_NOTIFICATION — Master to Client (push, requestId=0).
	// Annonce qu'un match a ete forme : opponent (V1 : AI fictive) et rating
	// adverse. Le client doit confirmer via MatchAccept (accept=true|false).
	// =========================================================================

	/// Wire format :
	///   uint32 proposalId
	///   string opponentTeamName  (uint16 length + UTF-8)
	///   uint32 opponentRating
	struct ArenaMatchProposalNotificationPayload
	{
		uint32_t    proposalId       = 0;
		std::string opponentTeamName;
		uint32_t    opponentRating   = 0;
	};

	// =========================================================================
	// ARENA_MATCH_ACCEPT — Client to Master : accepte ou rejette une proposal.
	// =========================================================================

	/// Wire format :
	///   uint32 proposalId
	///   uint8  accept            (0 = reject, 1 = accept ; serialise via uint8)
	struct ArenaMatchAcceptRequestPayload
	{
		uint32_t proposalId = 0;
		bool     accept     = false;
	};

	/// Wire format :
	///   uint8 error
	struct ArenaMatchAcceptResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// ARENA_MATCH_RESULT_NOTIFICATION — Master to Client (push, requestId=0).
	// Annonce le resultat du match avec le nouveau rating ELO.
	// =========================================================================

	/// Wire format :
	///   uint8  win                (0 = loss, 1 = win)
	///   uint32 oldRating
	///   uint32 newRating
	///   string opponentName       (uint16 length + UTF-8)
	struct ArenaMatchResultNotificationPayload
	{
		bool        win        = false;
		uint32_t    oldRating  = 0;
		uint32_t    newRating  = 0;
		std::string opponentName;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	std::optional<ArenaTeamListRequestPayload>      ParseArenaTeamListRequestPayload     (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaQueueRequestPayload>         ParseArenaQueueRequestPayload        (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaLeaveQueueRequestPayload>    ParseArenaLeaveQueueRequestPayload   (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaMatchAcceptRequestPayload>   ParseArenaMatchAcceptRequestPayload  (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildArenaTeamListRequestPayload();
	std::vector<uint8_t> BuildArenaQueueRequestPayload(uint32_t teamId, uint8_t size);
	std::vector<uint8_t> BuildArenaLeaveQueueRequestPayload();
	std::vector<uint8_t> BuildArenaMatchAcceptRequestPayload(uint32_t proposalId, bool accept);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses (payload-only)
	// -------------------------------------------------------------------------

	std::optional<ArenaTeamListResponsePayload>             ParseArenaTeamListResponsePayload          (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaQueueResponsePayload>                ParseArenaQueueResponsePayload             (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaLeaveQueueResponsePayload>           ParseArenaLeaveQueueResponsePayload        (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaMatchProposalNotificationPayload>    ParseArenaMatchProposalNotificationPayload (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaMatchAcceptResponsePayload>          ParseArenaMatchAcceptResponsePayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<ArenaMatchResultNotificationPayload>      ParseArenaMatchResultNotificationPayload   (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildArenaTeamListResponsePayload          (uint8_t error, const std::vector<ArenaTeamSummary>& teams);
	std::vector<uint8_t> BuildArenaQueueResponsePayload             (uint8_t error, uint32_t estimatedWaitSec);
	std::vector<uint8_t> BuildArenaLeaveQueueResponsePayload        (uint8_t error);
	std::vector<uint8_t> BuildArenaMatchProposalNotificationPayload (uint32_t proposalId, const std::string& opponentTeamName, uint32_t opponentRating);
	std::vector<uint8_t> BuildArenaMatchAcceptResponsePayload       (uint8_t error);
	std::vector<uint8_t> BuildArenaMatchResultNotificationPayload   (bool win, uint32_t oldRating, uint32_t newRating, const std::string& opponentName);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildArenaTeamListResponsePacket          (uint8_t error, const std::vector<ArenaTeamSummary>& teams,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildArenaQueueResponsePacket             (uint8_t error, uint32_t estimatedWaitSec,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildArenaLeaveQueueResponsePacket        (uint8_t error,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildArenaMatchAcceptResponsePacket       (uint8_t error,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildArenaMatchProposalNotificationPacket (uint32_t proposalId, const std::string& opponentTeamName, uint32_t opponentRating,
	                                                                 uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildArenaMatchResultNotificationPacket   (bool win, uint32_t oldRating, uint32_t newRating, const std::string& opponentName,
	                                                                 uint64_t sessionIdHeader);
}
