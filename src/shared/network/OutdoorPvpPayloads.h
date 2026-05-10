#pragma once
// CMANGOS.36 (Phase 5.36 step 3+4) — Wire payloads pour les opcodes OutdoorPvP
// (140-149). 4 paires Request/Response + 2 push notifications :
//   - ZoneList                          (140/141)
//   - Subscribe                         (142/143)
//   - Unsubscribe                       (144/145)
//   - CaptureStart                      (146/147)
//   - CaptureProgressNotification       (148 push) : push Master to Client
//     progression capture en cours (capturePct + capturingBy).
//   - CaptureCompletedNotification      (149 push) : push Master to Client
//     capture finie (newOwner + scores updated).
//
// Le master tient en memoire un OutdoorPvPManager (V1 : 2 zones hardcodees).
// Au reboot, subscriptions et progression captures sont perdues. Acceptable V1.
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
	// Codes d'erreur — wire-level pour OutdoorPvP.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes OutdoorPvP. 0 = OK.
	enum class OutdoorPvpErrorCode : uint8_t
	{
		Ok                = 0,
		UnknownZone       = 1, ///< zoneId pas dans la liste hardcodee V1.
		NotSubscribed     = 2, ///< Tentative d'unsubscribe sans subscription prealable.
		UnknownObjective  = 3, ///< objectiveId pas dans la zone.
		InvalidFaction    = 4, ///< faction hors {0=Alliance, 1=Horde}.
		Unauthorized      = 5, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// Sous-structs partages — Objective summary + Zone summary.
	// =========================================================================

	/// Resume d'un objectif capturable pour le wire.
	/// Wire format :
	///   uint32 objectiveId
	///   uint8  owner          (0=Alliance, 1=Horde, 0xFF=neutral)
	///   uint32 capturePct     (0..100)
	///   uint8  capturingBy    (0xFF si aucune capture en cours)
	struct OutdoorPvpObjectiveSummary
	{
		uint32_t objectiveId  = 0;
		uint8_t  owner        = 0xFFu;
		uint32_t capturePct   = 0;
		uint8_t  capturingBy  = 0xFFu;
	};

	/// Resume d'une zone contestee pour le wire.
	/// Wire format :
	///   uint32 zoneId
	///   string name           (uint16 length + UTF-8)
	///   uint32 allianceScore
	///   uint32 hordeScore
	///   uint16 objectiveCount
	///   <count> OutdoorPvpObjectiveSummary
	struct OutdoorPvpZoneSummary
	{
		uint32_t                                 zoneId         = 0;
		std::string                              name;
		uint32_t                                 allianceScore  = 0;
		uint32_t                                 hordeScore     = 0;
		std::vector<OutdoorPvpObjectiveSummary>  objectives;
	};

	// =========================================================================
	// OUTDOOR_PVP_ZONE_LIST — Client to Master : liste des zones contestees.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct OutdoorPvpZoneListRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8  error                     (cf. OutdoorPvpErrorCode)
	///   uint16 zoneCount                 (si error == 0)
	///   <count> OutdoorPvpZoneSummary
	struct OutdoorPvpZoneListResponsePayload
	{
		uint8_t                              error = 0;
		std::vector<OutdoorPvpZoneSummary>   zones;
	};

	// =========================================================================
	// OUTDOOR_PVP_SUBSCRIBE — Client to Master : s'abonne aux push d'une zone.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	struct OutdoorPvpSubscribeRequestPayload
	{
		uint32_t zoneId = 0;
	};

	/// Wire format :
	///   uint8 error
	struct OutdoorPvpSubscribeResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// OUTDOOR_PVP_UNSUBSCRIBE — Client to Master : se desabonne.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	struct OutdoorPvpUnsubscribeRequestPayload
	{
		uint32_t zoneId = 0;
	};

	/// Wire format :
	///   uint8 error
	struct OutdoorPvpUnsubscribeResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// OUTDOOR_PVP_CAPTURE_START — Client to Master : capture un objectif.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	///   uint32 objectiveId
	///   uint8  faction           (0=Alliance, 1=Horde)
	struct OutdoorPvpCaptureStartRequestPayload
	{
		uint32_t zoneId      = 0;
		uint32_t objectiveId = 0;
		uint8_t  faction     = 0;
	};

	/// Wire format :
	///   uint8 error
	struct OutdoorPvpCaptureStartResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// OUTDOOR_PVP_CAPTURE_PROGRESS_NOTIFICATION — Master to Client (push, requestId=0).
	// Update transitoire de progression de capture.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	///   uint32 objectiveId
	///   uint32 capturePct       (0..100)
	///   uint8  capturingBy      (0=Alliance, 1=Horde, 0xFF si aucune)
	struct OutdoorPvpCaptureProgressNotificationPayload
	{
		uint32_t zoneId       = 0;
		uint32_t objectiveId  = 0;
		uint32_t capturePct   = 0;
		uint8_t  capturingBy  = 0xFFu;
	};

	// =========================================================================
	// OUTDOOR_PVP_CAPTURE_COMPLETED_NOTIFICATION — Master to Client (push, requestId=0).
	// Fin de capture : new owner + scores mis a jour.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	///   uint32 objectiveId
	///   uint8  newOwner         (0=Alliance, 1=Horde, 0xFF si reset)
	///   uint32 allianceScore
	///   uint32 hordeScore
	struct OutdoorPvpCaptureCompletedNotificationPayload
	{
		uint32_t zoneId         = 0;
		uint32_t objectiveId    = 0;
		uint8_t  newOwner       = 0xFFu;
		uint32_t allianceScore  = 0;
		uint32_t hordeScore     = 0;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpZoneListRequestPayload>     ParseOutdoorPvpZoneListRequestPayload    (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpSubscribeRequestPayload>    ParseOutdoorPvpSubscribeRequestPayload   (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpUnsubscribeRequestPayload>  ParseOutdoorPvpUnsubscribeRequestPayload (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpCaptureStartRequestPayload> ParseOutdoorPvpCaptureStartRequestPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildOutdoorPvpZoneListRequestPayload();
	std::vector<uint8_t> BuildOutdoorPvpSubscribeRequestPayload   (uint32_t zoneId);
	std::vector<uint8_t> BuildOutdoorPvpUnsubscribeRequestPayload (uint32_t zoneId);
	std::vector<uint8_t> BuildOutdoorPvpCaptureStartRequestPayload(uint32_t zoneId, uint32_t objectiveId, uint8_t faction);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses & Notifications (payload-only)
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpZoneListResponsePayload>                     ParseOutdoorPvpZoneListResponsePayload                  (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpSubscribeResponsePayload>                    ParseOutdoorPvpSubscribeResponsePayload                 (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpUnsubscribeResponsePayload>                  ParseOutdoorPvpUnsubscribeResponsePayload               (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpCaptureStartResponsePayload>                 ParseOutdoorPvpCaptureStartResponsePayload              (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpCaptureProgressNotificationPayload>          ParseOutdoorPvpCaptureProgressNotificationPayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<OutdoorPvpCaptureCompletedNotificationPayload>         ParseOutdoorPvpCaptureCompletedNotificationPayload      (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildOutdoorPvpZoneListResponsePayload                  (uint8_t error, const std::vector<OutdoorPvpZoneSummary>& zones);
	std::vector<uint8_t> BuildOutdoorPvpSubscribeResponsePayload                 (uint8_t error);
	std::vector<uint8_t> BuildOutdoorPvpUnsubscribeResponsePayload               (uint8_t error);
	std::vector<uint8_t> BuildOutdoorPvpCaptureStartResponsePayload              (uint8_t error);
	std::vector<uint8_t> BuildOutdoorPvpCaptureProgressNotificationPayload       (uint32_t zoneId, uint32_t objectiveId, uint32_t capturePct, uint8_t capturingBy);
	std::vector<uint8_t> BuildOutdoorPvpCaptureCompletedNotificationPayload      (uint32_t zoneId, uint32_t objectiveId, uint8_t newOwner,
	                                                                              uint32_t allianceScore, uint32_t hordeScore);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildOutdoorPvpZoneListResponsePacket                   (uint8_t error, const std::vector<OutdoorPvpZoneSummary>& zones,
	                                                                              uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildOutdoorPvpSubscribeResponsePacket                  (uint8_t error, uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildOutdoorPvpUnsubscribeResponsePacket                (uint8_t error, uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildOutdoorPvpCaptureStartResponsePacket               (uint8_t error, uint32_t requestId, uint64_t sessionIdHeader);

	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildOutdoorPvpCaptureProgressNotificationPacket        (uint32_t zoneId, uint32_t objectiveId, uint32_t capturePct, uint8_t capturingBy,
	                                                                              uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildOutdoorPvpCaptureCompletedNotificationPacket       (uint32_t zoneId, uint32_t objectiveId, uint8_t newOwner,
	                                                                              uint32_t allianceScore, uint32_t hordeScore,
	                                                                              uint64_t sessionIdHeader);
}
