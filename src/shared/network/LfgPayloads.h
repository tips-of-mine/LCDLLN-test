#pragma once
// CMANGOS.33 (Phase 5.33 step 3+4) — Wire payloads pour les opcodes Lfg
// (100-107). 4 paires Request/Response + 1 push :
//   - Queue                  (100/101)
//   - Leave                  (102/103)
//   - Status                 (104/105)
//   - MatchProposalNotification (106 push) : push Master to Client annonce
//     un groupe forme.
//   - MatchAccept            (107) : pas de response payload separee en V1
//     (le master se contente de logger et send Ok via une reponse minimale).
//
// La file LookForGroup est gardee cote master uniquement (pas de DB) : au
// reboot, toutes les inscriptions sont perdues. Acceptable V1.
//
// Format wire : ByteReader/ByteWriter little-endian. Toutes les strings
// passent par WriteString/ReadString (uint16 length + UTF-8 bytes), et
// toutes les arrays par WriteArrayCount/ReadArrayCount (uint16 count).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Lfg.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Lfg. 0 = OK.
	enum class LfgErrorCode : uint8_t
	{
		Ok             = 0,
		AlreadyQueued  = 1, ///< Le joueur est deja dans une queue (Queue request seulement).
		NotInQueue     = 2, ///< Pas inscrit dans une queue (Leave / Status request).
		InvalidRole    = 3, ///< role > 2 (Damage = 2 max).
		InvalidDungeon = 4, ///< dungeonId == 0 (V1 : pas de catalogue cote master).
		MatchExpired   = 5, ///< proposalId inconnu / expire (MatchAccept).
		Unauthorized   = 6, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// LFG_QUEUE — Client to Master : inscription a la queue.
	// =========================================================================

	/// Wire format :
	///   uint8  role        (0=Tank, 1=Healer, 2=Damage ; cf. LfgRole)
	///   uint32 dungeonId   (id opaque cote V1)
	struct LfgQueueRequestPayload
	{
		uint8_t  role      = 0;
		uint32_t dungeonId = 0;
	};

	/// Wire format :
	///   uint8  error              (cf. LfgErrorCode)
	///   uint32 estimatedWaitSec   (si error == 0)
	struct LfgQueueResponsePayload
	{
		uint8_t  error             = 0;
		uint32_t estimatedWaitSec  = 0;
	};

	// =========================================================================
	// LFG_LEAVE — Client to Master : quitter la queue.
	// =========================================================================

	/// Wire format : (vide). L'account et le dungeonId courant sont derives
	/// cote master (V1 : un seul player => un seul dungeon en queue).
	struct LfgLeaveRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8 error
	struct LfgLeaveResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// LFG_STATUS — Client to Master : interroge l'etat de queue du joueur.
	// =========================================================================

	/// Wire format : (vide).
	struct LfgStatusRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8  error
	///   uint8  inQueue       (0=non, 1=oui ; bool serialise sur 1 octet)
	///   uint8  role          (significatif si inQueue == 1)
	///   uint32 dungeonId     (significatif si inQueue == 1)
	///   uint32 elapsedSec    (depuis joinedTsMs ; 0 si pas en queue)
	struct LfgStatusResponsePayload
	{
		uint8_t  error      = 0;
		bool     inQueue    = false;
		uint8_t  role       = 0;
		uint32_t dungeonId  = 0;
		uint32_t elapsedSec = 0;
	};

	// =========================================================================
	// LFG_MATCH_PROPOSAL_NOTIFICATION — Master to Client (push, requestId=0).
	// Annonce qu'un groupe a ete forme et qu'une proposition est en attente
	// de confirmation.
	// =========================================================================

	/// Une entree de la liste de membres d'une match proposal.
	struct LfgMatchMember
	{
		uint64_t accountId = 0;
		uint8_t  role      = 0;
	};

	/// Wire format :
	///   uint64 proposalId       (id opaque V1, 0 accepte)
	///   uint32 dungeonId
	///   uint16 memberCount
	///   <count> members         (8 + 1 octets chacune)
	struct LfgMatchProposalNotificationPayload
	{
		uint64_t                   proposalId = 0;
		uint32_t                   dungeonId  = 0;
		std::vector<LfgMatchMember> members;
	};

	// =========================================================================
	// LFG_MATCH_ACCEPT — Client to Master : accepte ou rejette une proposal.
	// V1 simplifie : pas de payload de reponse separee, le master logue et
	// envoie LfgQueueResponse (Ok) en echo.
	// =========================================================================

	/// Wire format :
	///   uint64 proposalId
	///   uint8  accept         (0 = reject, 1 = accept ; serialise via uint8)
	struct LfgMatchAcceptRequestPayload
	{
		uint64_t proposalId = 0;
		bool     accept     = false;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	std::optional<LfgQueueRequestPayload>        ParseLfgQueueRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::optional<LfgLeaveRequestPayload>        ParseLfgLeaveRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::optional<LfgStatusRequestPayload>       ParseLfgStatusRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::optional<LfgMatchAcceptRequestPayload>  ParseLfgMatchAcceptRequestPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildLfgQueueRequestPayload(uint8_t role, uint32_t dungeonId);
	std::vector<uint8_t> BuildLfgLeaveRequestPayload();
	std::vector<uint8_t> BuildLfgStatusRequestPayload();
	std::vector<uint8_t> BuildLfgMatchAcceptRequestPayload(uint64_t proposalId, bool accept);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses (payload-only)
	// -------------------------------------------------------------------------

	std::optional<LfgQueueResponsePayload>                ParseLfgQueueResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<LfgLeaveResponsePayload>                ParseLfgLeaveResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<LfgStatusResponsePayload>               ParseLfgStatusResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<LfgMatchProposalNotificationPayload>    ParseLfgMatchProposalNotificationPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildLfgQueueResponsePayload(uint8_t error, uint32_t estimatedWaitSec);
	std::vector<uint8_t> BuildLfgLeaveResponsePayload(uint8_t error);
	std::vector<uint8_t> BuildLfgStatusResponsePayload(uint8_t error, bool inQueue, uint8_t role, uint32_t dungeonId, uint32_t elapsedSec);
	std::vector<uint8_t> BuildLfgMatchProposalNotificationPayload(uint64_t proposalId, uint32_t dungeonId, const std::vector<LfgMatchMember>& members);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildLfgQueueResponsePacket(uint8_t error, uint32_t estimatedWaitSec,
	                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildLfgLeaveResponsePacket(uint8_t error,
	                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildLfgStatusResponsePacket(uint8_t error, bool inQueue, uint8_t role, uint32_t dungeonId, uint32_t elapsedSec,
	                                                  uint32_t requestId, uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildLfgMatchProposalNotificationPacket(uint64_t proposalId, uint32_t dungeonId, const std::vector<LfgMatchMember>& members,
	                                                              uint64_t sessionIdHeader);
}
