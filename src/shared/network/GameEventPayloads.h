#pragma once
// CMANGOS.31 (Phase 5.31 step 3+4) — Wire payloads pour les opcodes GameEvent
// (157-163). 3 paires Request/Response + 1 push notification :
//   - List                              (157/158)
//   - Subscribe                         (159/160)
//   - Unsubscribe                       (161/162)
//   - StateChangeNotification           (163 push) : push Master to Client
//     changement d'etat event (eventId, newState, untilTsMs).
//
// Le master tient en memoire un GameEventManager (V1 : 4 events hardcodes
// Halloween, Winter Veil, Lunar Festival, Midsummer Fire). Au reboot,
// subscriptions et lastBroadcastState sont reinitialises. Acceptable V1.
//
// Format wire : ByteReader/ByteWriter little-endian. Strings via
// WriteString/ReadString (uint16 length + UTF-8 bytes), arrays via
// WriteArrayCount/ReadArrayCount (uint16 count). Les uint64 (timestamps
// ms depuis epoch, durations ms) passent par WriteU64/ReadU64.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour GameEvent.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes GameEvent. 0 = OK.
	enum class GameEventErrorCode : uint8_t
	{
		Ok                = 0,
		AlreadySubscribed = 1, ///< Tentative de subscribe quand deja abonne.
		NotSubscribed     = 2, ///< Tentative d'unsubscribe sans subscription prealable.
		Unauthorized      = 3, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// Sous-struct partage — Event summary.
	// =========================================================================

	/// Resume d'un event saisonnier pour le wire.
	/// Wire format :
	///   uint32 eventId
	///   string name           (uint16 length + UTF-8)
	///   uint8  state          (Inactive=0, Active=1)
	///   uint64 startTsMs      (ms depuis epoch, debut absolu/recurrent)
	///   uint64 durationMs     (duree de chaque occurrence)
	///   uint64 recurMs        (0 = one-shot ; >0 = periode de recurrence)
	struct GameEventSummary
	{
		uint32_t    eventId    = 0;
		std::string name;
		uint8_t     state      = 0;     ///< 0=Inactive, 1=Active.
		uint64_t    startTsMs  = 0;
		uint64_t    durationMs = 0;
		uint64_t    recurMs    = 0;
	};

	// =========================================================================
	// GAME_EVENT_LIST — Client to Master : liste des events.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct GameEventListRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8  error                     (cf. GameEventErrorCode)
	///   uint16 eventCount                (si error == 0)
	///   <count> GameEventSummary
	struct GameEventListResponsePayload
	{
		uint8_t                         error = 0;
		std::vector<GameEventSummary>   events;
	};

	// =========================================================================
	// GAME_EVENT_SUBSCRIBE — Client to Master : s'abonne aux push.
	// =========================================================================

	/// Wire format : (vide). Abonnement global, pas par event.
	struct GameEventSubscribeRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8 error
	struct GameEventSubscribeResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// GAME_EVENT_UNSUBSCRIBE — Client to Master : se desabonne.
	// =========================================================================

	/// Wire format : (vide).
	struct GameEventUnsubscribeRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8 error
	struct GameEventUnsubscribeResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// GAME_EVENT_STATE_CHANGE_NOTIFICATION — Master to Client (push, requestId=0).
	// Changement d'etat d'un event (devient Active ou Inactive).
	// =========================================================================

	/// Wire format :
	///   uint32 eventId
	///   uint8  newState        (0=Inactive, 1=Active)
	///   uint64 untilTsMs       (timestamp absolu ms : si Active = quand finira ;
	///                           si Inactive = quand recommencera ;
	///                           0 = pas de prochaine bascule connue)
	struct GameEventStateChangeNotificationPayload
	{
		uint32_t eventId    = 0;
		uint8_t  newState   = 0;
		uint64_t untilTsMs  = 0;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	std::optional<GameEventListRequestPayload>        ParseGameEventListRequestPayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<GameEventSubscribeRequestPayload>   ParseGameEventSubscribeRequestPayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<GameEventUnsubscribeRequestPayload> ParseGameEventUnsubscribeRequestPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildGameEventListRequestPayload();
	std::vector<uint8_t> BuildGameEventSubscribeRequestPayload();
	std::vector<uint8_t> BuildGameEventUnsubscribeRequestPayload();

	// -------------------------------------------------------------------------
	// Parse / Build — Responses & Notifications (payload-only)
	// -------------------------------------------------------------------------

	std::optional<GameEventListResponsePayload>             ParseGameEventListResponsePayload             (const uint8_t* payload, size_t payloadSize);
	std::optional<GameEventSubscribeResponsePayload>        ParseGameEventSubscribeResponsePayload        (const uint8_t* payload, size_t payloadSize);
	std::optional<GameEventUnsubscribeResponsePayload>      ParseGameEventUnsubscribeResponsePayload      (const uint8_t* payload, size_t payloadSize);
	std::optional<GameEventStateChangeNotificationPayload>  ParseGameEventStateChangeNotificationPayload  (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildGameEventListResponsePayload            (uint8_t error, const std::vector<GameEventSummary>& events);
	std::vector<uint8_t> BuildGameEventSubscribeResponsePayload       (uint8_t error);
	std::vector<uint8_t> BuildGameEventUnsubscribeResponsePayload     (uint8_t error);
	std::vector<uint8_t> BuildGameEventStateChangeNotificationPayload (uint32_t eventId, uint8_t newState, uint64_t untilTsMs);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildGameEventListResponsePacket            (uint8_t error, const std::vector<GameEventSummary>& events,
	                                                                  uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildGameEventSubscribeResponsePacket       (uint8_t error, uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildGameEventUnsubscribeResponsePacket     (uint8_t error, uint32_t requestId, uint64_t sessionIdHeader);

	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildGameEventStateChangeNotificationPacket (uint32_t eventId, uint8_t newState, uint64_t untilTsMs,
	                                                                  uint64_t sessionIdHeader);
}
