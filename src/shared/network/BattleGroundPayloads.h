#pragma once
// CMANGOS.10 (Phase 5 step 3+4) — Wire payloads pour les opcodes BattleGround
// (130-139). 3 paires Request/Response + 3 push notifications + 1 Request push :
//   - List                            (130/131)
//   - Queue                           (132/133)
//   - LeaveQueue                      (134/135)
//   - MatchStartNotification          (136 push) : push Master to Client
//     annonce qu'un match BG a demarre.
//   - ScoreUpdateNotification         (137 push) : push Master to Client
//     update les scores Alliance/Horde + elapsed.
//   - MatchEndNotification            (138 push) : push Master to Client
//     annonce fin de match (winnerFaction + final scores + duration).
//   - LeaveMatch                      (139 push) : Client to Master,
//     forfait V1 (fire-and-forget, pas de Response paire).
//
// Le BattleGroundQueue est garde cote master uniquement en V1 (pas de DB) :
// au reboot, queue + matches actifs sont perdus. Acceptable V1.
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
	// Codes d'erreur — wire-level pour BattleGround.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes BattleGround. 0 = OK.
	enum class BgErrorCode : uint8_t
	{
		Ok               = 0,
		AlreadyQueued    = 1, ///< Le compte est deja inscrit dans une queue BG.
		UnknownBg        = 2, ///< bgType inconnu (1=Warsong, 2=Arathi, 3=Alterac V1).
		InvalidFaction   = 3, ///< faction hors {0=Alliance, 1=Horde}.
		NotInQueue       = 4, ///< Pas inscrit dans une queue BG (LeaveQueue).
		Unauthorized     = 5, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// BG_LIST — Client to Master : liste des battlegrounds disponibles.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct BgListRequestPayload
	{
		// (vide)
	};

	/// Resume d'un battleground pour le wire.
	/// Wire format :
	///   uint16 bgType
	///   string name           (uint16 length + UTF-8)
	///   uint8  teamSize       (10, 15 ou 40 V1)
	///   string mapName        (uint16 length + UTF-8)
	struct BgInfo
	{
		uint16_t    bgType   = 0;
		std::string name;
		uint8_t     teamSize = 0;
		std::string mapName;
	};

	/// Wire format :
	///   uint8  error           (cf. BgErrorCode)
	///   uint16 count            (si error == 0)
	///   <count> BgInfo
	struct BgListResponsePayload
	{
		uint8_t              error = 0;
		std::vector<BgInfo>  battlegrounds;
	};

	// =========================================================================
	// BG_QUEUE — Client to Master : inscription a la queue BG.
	// =========================================================================

	/// Wire format :
	///   uint16 bgType
	///   uint8  faction         (0=Alliance, 1=Horde)
	struct BgQueueRequestPayload
	{
		uint16_t bgType  = 0;
		uint8_t  faction = 0;
	};

	/// Wire format :
	///   uint8  error             (cf. BgErrorCode)
	///   uint32 estimatedWaitSec  (si error == 0)
	///   uint32 queuePosition     (si error == 0)
	struct BgQueueResponsePayload
	{
		uint8_t  error            = 0;
		uint32_t estimatedWaitSec = 0;
		uint32_t queuePosition    = 0;
	};

	// =========================================================================
	// BG_LEAVE_QUEUE — Client to Master : quitte la queue BG.
	// =========================================================================

	/// Wire format : (vide). L'account derive de la session cote master.
	struct BgLeaveQueueRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8 error
	struct BgLeaveQueueResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// BG_MATCH_START_NOTIFICATION — Master to Client (push, requestId=0).
	// Annonce qu'un match BG a demarre. Le client met a jour son scoreboard.
	// =========================================================================

	/// Wire format :
	///   uint64 matchId
	///   uint16 bgType
	///   string mapName        (uint16 length + UTF-8)
	///   uint8  allianceCount
	///   uint8  hordeCount
	struct BgMatchStartNotificationPayload
	{
		uint64_t    matchId        = 0;
		uint16_t    bgType         = 0;
		std::string mapName;
		uint8_t     allianceCount  = 0;
		uint8_t     hordeCount     = 0;
	};

	// =========================================================================
	// BG_SCORE_UPDATE_NOTIFICATION — Master to Client (push, requestId=0).
	// Update transitoire des scores pendant le match.
	// =========================================================================

	/// Wire format :
	///   uint64 matchId
	///   uint32 allianceScore
	///   uint32 hordeScore
	///   uint32 elapsedSec
	struct BgScoreUpdateNotificationPayload
	{
		uint64_t matchId       = 0;
		uint32_t allianceScore = 0;
		uint32_t hordeScore    = 0;
		uint32_t elapsedSec    = 0;
	};

	// =========================================================================
	// BG_MATCH_END_NOTIFICATION — Master to Client (push, requestId=0).
	// Annonce fin de match avec gagnant et scores finaux.
	// winnerFaction : 0=Alliance, 1=Horde, 2=Draw.
	// =========================================================================

	/// Wire format :
	///   uint64 matchId
	///   uint8  winnerFaction   (0/1/2)
	///   uint32 allianceScore
	///   uint32 hordeScore
	///   uint32 durationSec
	struct BgMatchEndNotificationPayload
	{
		uint64_t matchId        = 0;
		uint8_t  winnerFaction  = 0;
		uint32_t allianceScore  = 0;
		uint32_t hordeScore     = 0;
		uint32_t durationSec    = 0;
	};

	// =========================================================================
	// BG_LEAVE_MATCH — Client to Master : forfait du match en cours.
	// Pas de Response paire (fire-and-forget V1) ; le master pousse a la
	// place un BgMatchEndNotification avec winnerFaction = opposite.
	// =========================================================================

	/// Wire format : (vide). L'account derive de la session cote master.
	struct BgLeaveMatchRequestPayload
	{
		// (vide)
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	std::optional<BgListRequestPayload>        ParseBgListRequestPayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<BgQueueRequestPayload>       ParseBgQueueRequestPayload      (const uint8_t* payload, size_t payloadSize);
	std::optional<BgLeaveQueueRequestPayload>  ParseBgLeaveQueueRequestPayload (const uint8_t* payload, size_t payloadSize);
	std::optional<BgLeaveMatchRequestPayload>  ParseBgLeaveMatchRequestPayload (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildBgListRequestPayload();
	std::vector<uint8_t> BuildBgQueueRequestPayload(uint16_t bgType, uint8_t faction);
	std::vector<uint8_t> BuildBgLeaveQueueRequestPayload();
	std::vector<uint8_t> BuildBgLeaveMatchRequestPayload();

	// -------------------------------------------------------------------------
	// Parse / Build — Responses & Notifications (payload-only)
	// -------------------------------------------------------------------------

	std::optional<BgListResponsePayload>                ParseBgListResponsePayload               (const uint8_t* payload, size_t payloadSize);
	std::optional<BgQueueResponsePayload>               ParseBgQueueResponsePayload              (const uint8_t* payload, size_t payloadSize);
	std::optional<BgLeaveQueueResponsePayload>          ParseBgLeaveQueueResponsePayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<BgMatchStartNotificationPayload>      ParseBgMatchStartNotificationPayload     (const uint8_t* payload, size_t payloadSize);
	std::optional<BgScoreUpdateNotificationPayload>     ParseBgScoreUpdateNotificationPayload    (const uint8_t* payload, size_t payloadSize);
	std::optional<BgMatchEndNotificationPayload>        ParseBgMatchEndNotificationPayload       (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildBgListResponsePayload                (uint8_t error, const std::vector<BgInfo>& battlegrounds);
	std::vector<uint8_t> BuildBgQueueResponsePayload               (uint8_t error, uint32_t estimatedWaitSec, uint32_t queuePosition);
	std::vector<uint8_t> BuildBgLeaveQueueResponsePayload          (uint8_t error);
	std::vector<uint8_t> BuildBgMatchStartNotificationPayload      (uint64_t matchId, uint16_t bgType, const std::string& mapName,
	                                                                 uint8_t allianceCount, uint8_t hordeCount);
	std::vector<uint8_t> BuildBgScoreUpdateNotificationPayload     (uint64_t matchId, uint32_t allianceScore, uint32_t hordeScore, uint32_t elapsedSec);
	std::vector<uint8_t> BuildBgMatchEndNotificationPayload        (uint64_t matchId, uint8_t winnerFaction,
	                                                                 uint32_t allianceScore, uint32_t hordeScore, uint32_t durationSec);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildBgListResponsePacket                 (uint8_t error, const std::vector<BgInfo>& battlegrounds,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildBgQueueResponsePacket                (uint8_t error, uint32_t estimatedWaitSec, uint32_t queuePosition,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildBgLeaveQueueResponsePacket           (uint8_t error,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);

	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildBgMatchStartNotificationPacket       (uint64_t matchId, uint16_t bgType, const std::string& mapName,
	                                                                 uint8_t allianceCount, uint8_t hordeCount,
	                                                                 uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildBgScoreUpdateNotificationPacket      (uint64_t matchId, uint32_t allianceScore, uint32_t hordeScore, uint32_t elapsedSec,
	                                                                 uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildBgMatchEndNotificationPacket         (uint64_t matchId, uint8_t winnerFaction,
	                                                                 uint32_t allianceScore, uint32_t hordeScore, uint32_t durationSec,
	                                                                 uint64_t sessionIdHeader);
}
