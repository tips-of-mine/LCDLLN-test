#pragma once
// CMANGOS.27 (Phase 4.27 step 3+4) -- Wire payloads pour les opcodes Trade (83-94).
//
// 5 paires Request/Response + 3 push notifications :
//   - Begin        (83/84)  + push BeginNotification (85)
//   - SetOffer     (86/87)
//   - Lock         (88/89)
//   - StateUpdate  push (90)
//   - Commit       (91/92)
//   - Cancel       (93)     + push CancelNotification (94)
//
// Format wire : ByteReader/ByteWriter little-endian, vectors prefixes par
// uint16 count via WriteArrayCount/ReadArrayCount, strings length-prefixed
// (uint16 + UTF-8 bytes) via WriteString/ReadString.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur Trade -- wire-level. Mapping cote serveur depuis l'API
	// TradeSession + checks de routing dans TradeHandler.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Trade.
	/// 0 = OK ; sinon sessionId == 0 ou state non-significatif selon le cas.
	enum class TradeErrorCode : uint8_t
	{
		Ok                = 0,
		PartnerOffline    = 1,  ///< Le partenaire vise n'a pas de session active sur le master.
		PartnerInTrade    = 2,  ///< Sender ou target est deja dans une autre TradeSession.
		SelfTrade         = 3,  ///< Tentative de trade avec soi-meme.
		InvalidSession    = 4,  ///< sessionId inconnu cote registry.
		NotPartOfSession  = 5,  ///< L'expediteur n'est ni A ni B de la session ciblee.
		WrongState        = 6,  ///< Action FSM invalide (ex: Commit avant BothLocked).
		SessionTerminal   = 7,  ///< Session deja Committed ou Cancelled.
		Unauthorized      = 8,  ///< Pas de session valide cote master.
	};

	/// Plafond du nombre d'items par offer cote wire. Au-dela, le serveur
	/// retourne WrongState (ou tronque en silence) pour eviter un payload
	/// gigantesque. Aligne avec la limite UI (8 slots de trade, mais on
	/// laisse une marge pour stack splitting interne).
	inline constexpr size_t kMaxTradeItemsPerOffer = 16u;

	// =========================================================================
	// TRADE_BEGIN -- Client -> Master : initie une demande de trade.
	// =========================================================================

	/// Wire format : uint64 targetAccountId.
	struct TradeBeginRequestPayload
	{
		uint64_t targetAccountId = 0;
	};

	/// Wire format :
	///   uint8  error            (cf. TradeErrorCode)
	///   uint64 sessionId        (0 si error != Ok)
	///   uint64 partnerAccountId (0 si error != Ok)
	struct TradeBeginResponsePayload
	{
		uint8_t  error            = 0;
		uint64_t sessionId        = 0;
		uint64_t partnerAccountId = 0;
	};

	/// TRADE_BEGIN_NOTIFICATION (push, request_id=0). Envoye au target pour
	/// l'informer qu'un trade entrant a ete cree. Wire format :
	///   uint64 sessionId
	///   uint64 partnerAccountId   (l'initiateur, du point de vue du target)
	struct TradeBeginNotificationPayload
	{
		uint64_t sessionId        = 0;
		uint64_t partnerAccountId = 0;
	};

	// =========================================================================
	// TRADE_SET_OFFER -- Client -> Master : (re)definit l'offer cote sender.
	// =========================================================================

	/// Wire format :
	///   uint64 sessionId
	///   uint64 copperGold
	///   uint16 itemCount
	///   <itemCount> uint64 itemGuid
	struct TradeSetOfferRequestPayload
	{
		uint64_t              sessionId   = 0;
		uint64_t              copperGold  = 0;
		std::vector<uint64_t> itemGuids;
	};

	/// Wire format : uint8 error.
	struct TradeSetOfferResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// TRADE_LOCK -- Client -> Master : verrouille l'offer cote sender.
	// =========================================================================

	/// Wire format : uint64 sessionId.
	struct TradeLockRequestPayload
	{
		uint64_t sessionId = 0;
	};

	/// Wire format :
	///   uint8 error
	///   uint8 newState   (0=Open, 1=LockedA, 2=LockedB, 3=BothLocked, 4=Committed, 5=Cancelled)
	struct TradeLockResponsePayload
	{
		uint8_t error    = 0;
		uint8_t newState = 0;
	};

	// =========================================================================
	// TRADE_STATE_UPDATE_NOTIFICATION -- Master -> Client (push, request_id=0).
	// Pousse au partenaire a chaque changement (SetOffer / Lock / Commit /
	// Cancel cote autre joueur) pour rafraichir l'UI miroir.
	// =========================================================================

	/// Wire format :
	///   uint64 sessionId
	///   uint8  state             (FSM courant)
	///   uint64 partnerCopperGold (offer cote l'autre joueur)
	///   uint16 itemCount
	///   <itemCount> uint64 itemGuid
	struct TradeStateUpdateNotificationPayload
	{
		uint64_t              sessionId         = 0;
		uint8_t               state             = 0;
		uint64_t              partnerCopperGold = 0;
		std::vector<uint64_t> partnerItemGuids;
	};

	// =========================================================================
	// TRADE_COMMIT -- Client -> Master : finalise l'echange.
	// =========================================================================

	/// Wire format : uint64 sessionId.
	struct TradeCommitRequestPayload
	{
		uint64_t sessionId = 0;
	};

	/// Wire format : uint8 error.
	struct TradeCommitResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// TRADE_CANCEL -- Client -> Master : annule la trade.
	// =========================================================================

	/// Wire format : uint64 sessionId.
	struct TradeCancelRequestPayload
	{
		uint64_t sessionId = 0;
	};

	/// TRADE_CANCEL_NOTIFICATION (push, request_id=0). Envoye aux 2 participants
	/// pour annoncer l'annulation et fermer l'UI cote chacun. Wire format :
	///   uint64 sessionId
	///   string reason   (peut etre vide ; ex : "partner cancelled", "timeout")
	struct TradeCancelNotificationPayload
	{
		uint64_t    sessionId = 0;
		std::string reason;
	};

	// -------------------------------------------------------------------------
	// Parse / Build -- Requests
	// -------------------------------------------------------------------------

	std::optional<TradeBeginRequestPayload>     ParseTradeBeginRequestPayload    (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeSetOfferRequestPayload>  ParseTradeSetOfferRequestPayload (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeLockRequestPayload>      ParseTradeLockRequestPayload     (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeCommitRequestPayload>    ParseTradeCommitRequestPayload   (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeCancelRequestPayload>    ParseTradeCancelRequestPayload   (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildTradeBeginRequestPayload     (uint64_t targetAccountId);
	std::vector<uint8_t> BuildTradeSetOfferRequestPayload  (uint64_t sessionId, uint64_t copperGold,
	                                                         const std::vector<uint64_t>& itemGuids);
	std::vector<uint8_t> BuildTradeLockRequestPayload      (uint64_t sessionId);
	std::vector<uint8_t> BuildTradeCommitRequestPayload    (uint64_t sessionId);
	std::vector<uint8_t> BuildTradeCancelRequestPayload    (uint64_t sessionId);

	// -------------------------------------------------------------------------
	// Parse / Build -- Responses + push notifications (payload-only)
	// -------------------------------------------------------------------------

	std::optional<TradeBeginResponsePayload>             ParseTradeBeginResponsePayload            (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeBeginNotificationPayload>         ParseTradeBeginNotificationPayload        (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeSetOfferResponsePayload>          ParseTradeSetOfferResponsePayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeLockResponsePayload>              ParseTradeLockResponsePayload             (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeStateUpdateNotificationPayload>   ParseTradeStateUpdateNotificationPayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeCommitResponsePayload>            ParseTradeCommitResponsePayload           (const uint8_t* payload, size_t payloadSize);
	std::optional<TradeCancelNotificationPayload>        ParseTradeCancelNotificationPayload       (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildTradeBeginResponsePayload            (uint8_t error, uint64_t sessionId, uint64_t partnerAccountId);
	std::vector<uint8_t> BuildTradeBeginNotificationPayload        (uint64_t sessionId, uint64_t partnerAccountId);
	std::vector<uint8_t> BuildTradeSetOfferResponsePayload         (uint8_t error);
	std::vector<uint8_t> BuildTradeLockResponsePayload             (uint8_t error, uint8_t newState);
	std::vector<uint8_t> BuildTradeStateUpdateNotificationPayload  (uint64_t sessionId, uint8_t state,
	                                                                 uint64_t partnerCopperGold,
	                                                                 const std::vector<uint64_t>& partnerItemGuids);
	std::vector<uint8_t> BuildTradeCommitResponsePayload           (uint8_t error);
	std::vector<uint8_t> BuildTradeCancelNotificationPayload       (uint64_t sessionId, std::string_view reason);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildTradeBeginResponsePacket             (uint8_t error, uint64_t sessionId, uint64_t partnerAccountId,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildTradeBeginNotificationPacket         (uint64_t sessionId, uint64_t partnerAccountId,
	                                                                 uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildTradeSetOfferResponsePacket          (uint8_t error,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildTradeLockResponsePacket              (uint8_t error, uint8_t newState,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildTradeStateUpdateNotificationPacket   (uint64_t sessionId, uint8_t state,
	                                                                 uint64_t partnerCopperGold,
	                                                                 const std::vector<uint64_t>& partnerItemGuids,
	                                                                 uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildTradeCommitResponsePacket            (uint8_t error,
	                                                                 uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildTradeCancelNotificationPacket        (uint64_t sessionId, std::string_view reason,
	                                                                 uint64_t sessionIdHeader);
}
