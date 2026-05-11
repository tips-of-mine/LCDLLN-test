#pragma once
// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Wire payloads pour les
// opcodes Auction (173-181). 4 paires Request/Response + 1 push notification :
//   - List   (173/174)               : liste des listings (filter optionnel itemTemplateId).
//   - Post   (175/176)               : poste une nouvelle enchere.
//   - Bid    (177/178)               : place une bid (ou buyout immediat).
//   - Cancel (179/180)               : annule une enchere du sender.
//   - ExpiredNotification (181 push) : enchere expiree (vendue ou non).
//
// Le master tient en memoire un registry d'auctions (V1 : 8 listings seedees
// au boot avec differents owners). Les bids sont gerees inline. Pas de Tick
// periodique pour expirations : scan a chaque AuctionListRequest.
//
// Format wire : ByteReader/ByteWriter little-endian. Strings via
// WriteString/ReadString (uint16 length + UTF-8 bytes), arrays via
// WriteArrayCount/ReadArrayCount (uint16 count). Bool serialise en uint8 (0/1).
// Quantites monetaires en uint64 (copper).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Auction.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Auction. 0 = OK.
	enum class AuctionErrorCode : uint8_t
	{
		Ok               = 0,
		Unauthorized     = 1, ///< Pas de session valide cote master.
		InvalidParams    = 2, ///< Post : count=0, startBid=0, duration invalide, ou buyout < startBid.
		BidTooLow        = 3, ///< Bid : montant <= currentBid.
		AuctionExpired   = 4, ///< Bid/Cancel : auction terminee ou expiree.
		OwnAuction       = 5, ///< Bid : le bidder est le owner de l'auction.
		NotOwner         = 6, ///< Cancel : le sender n'est pas le owner.
		AuctionNotFound  = 7, ///< Cancel/Bid : auctionId inconnu du registry.
	};

	// =========================================================================
	// Sous-structs partages.
	// =========================================================================

	/// Resume d'une enchere active pour le wire (List response).
	/// Wire format :
	///   uint64 auctionId
	///   uint32 itemTemplateId
	///   string itemName
	///   uint32 count
	///   uint64 currentBidCopper
	///   uint64 buyoutCopper                 (0 = pas de buyout)
	///   string ownerName
	///   uint64 secondsUntilExpiration
	struct AuctionListingSummary
	{
		uint64_t    auctionId               = 0;
		uint32_t    itemTemplateId          = 0;
		std::string itemName;
		uint32_t    count                   = 0;
		uint64_t    currentBidCopper        = 0;
		uint64_t    buyoutCopper            = 0;
		std::string ownerName;
		uint64_t    secondsUntilExpiration  = 0;
	};

	// =========================================================================
	// AUCTION_LIST — Client to Master : liste les enchetes actives.
	// =========================================================================

	/// Wire format :
	///   uint32 itemTemplateIdFilter (0 = pas de filtre)
	struct AuctionListRequestPayload
	{
		uint32_t itemTemplateIdFilter = 0;
	};

	/// Wire format :
	///   uint8  error                       (cf. AuctionErrorCode)
	///   uint16 listingCount                (si error == 0)
	///   <count> AuctionListingSummary
	struct AuctionListResponsePayload
	{
		uint8_t                            error = 0;
		std::vector<AuctionListingSummary> listings;
	};

	// =========================================================================
	// AUCTION_POST — Client to Master : poste une nouvelle enchere.
	// =========================================================================

	/// Wire format :
	///   uint32 itemTemplateId
	///   uint32 count
	///   uint64 startBidCopper
	///   uint64 buyoutCopper       (0 = pas de buyout)
	///   uint8  durationHours      (12, 24 ou 48 V1)
	struct AuctionPostRequestPayload
	{
		uint32_t itemTemplateId  = 0;
		uint32_t count           = 0;
		uint64_t startBidCopper  = 0;
		uint64_t buyoutCopper    = 0;
		uint8_t  durationHours   = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint64 auctionId          (si error == 0)
	struct AuctionPostResponsePayload
	{
		uint8_t  error     = 0;
		uint64_t auctionId = 0;
	};

	// =========================================================================
	// AUCTION_BID — Client to Master : place une bid (ou buyout).
	// =========================================================================

	/// Wire format :
	///   uint64 auctionId
	///   uint64 bidAmountCopper
	struct AuctionBidRequestPayload
	{
		uint64_t auctionId       = 0;
		uint64_t bidAmountCopper = 0;
	};

	/// Wire format :
	///   uint8 error
	///   uint8 isBuyout         (0 = bid normale, 1 = buyout immediat)
	struct AuctionBidResponsePayload
	{
		uint8_t error    = 0;
		uint8_t isBuyout = 0;
	};

	// =========================================================================
	// AUCTION_CANCEL — Client to Master : annule sa propre enchere.
	// =========================================================================

	/// Wire format :
	///   uint64 auctionId
	struct AuctionCancelRequestPayload
	{
		uint64_t auctionId = 0;
	};

	/// Wire format :
	///   uint8 error
	struct AuctionCancelResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// AUCTION_EXPIRED_NOTIFICATION — Master to Client (push, requestId=0).
	// Enchere expiree : soit vendue (won=1), soit terminee sans bid (won=0).
	// =========================================================================

	/// Wire format :
	///   uint64 auctionId
	///   uint8  won                   (0 = pas de bid / 1 = vendue)
	///   uint64 finalBidCopper        (0 si won=0)
	///   string winnerName            (vide si won=0)
	struct AuctionExpiredNotificationPayload
	{
		uint64_t    auctionId       = 0;
		uint8_t     won             = 0;
		uint64_t    finalBidCopper  = 0;
		std::string winnerName;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests (payload-only)
	// -------------------------------------------------------------------------

	std::optional<AuctionListRequestPayload>    ParseAuctionListRequestPayload    (const uint8_t* payload, size_t payloadSize);
	std::optional<AuctionPostRequestPayload>    ParseAuctionPostRequestPayload    (const uint8_t* payload, size_t payloadSize);
	std::optional<AuctionBidRequestPayload>     ParseAuctionBidRequestPayload     (const uint8_t* payload, size_t payloadSize);
	std::optional<AuctionCancelRequestPayload>  ParseAuctionCancelRequestPayload  (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildAuctionListRequestPayload    (uint32_t itemTemplateIdFilter);
	std::vector<uint8_t> BuildAuctionPostRequestPayload    (uint32_t itemTemplateId, uint32_t count,
	                                                        uint64_t startBidCopper, uint64_t buyoutCopper,
	                                                        uint8_t durationHours);
	std::vector<uint8_t> BuildAuctionBidRequestPayload     (uint64_t auctionId, uint64_t bidAmountCopper);
	std::vector<uint8_t> BuildAuctionCancelRequestPayload  (uint64_t auctionId);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses & Notifications (payload-only)
	// -------------------------------------------------------------------------

	std::optional<AuctionListResponsePayload>          ParseAuctionListResponsePayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<AuctionPostResponsePayload>          ParseAuctionPostResponsePayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<AuctionBidResponsePayload>           ParseAuctionBidResponsePayload          (const uint8_t* payload, size_t payloadSize);
	std::optional<AuctionCancelResponsePayload>        ParseAuctionCancelResponsePayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<AuctionExpiredNotificationPayload>   ParseAuctionExpiredNotificationPayload  (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildAuctionListResponsePayload          (uint8_t error, const std::vector<AuctionListingSummary>& listings);
	std::vector<uint8_t> BuildAuctionPostResponsePayload          (uint8_t error, uint64_t auctionId);
	std::vector<uint8_t> BuildAuctionBidResponsePayload           (uint8_t error, uint8_t isBuyout);
	std::vector<uint8_t> BuildAuctionCancelResponsePayload        (uint8_t error);
	std::vector<uint8_t> BuildAuctionExpiredNotificationPayload   (uint64_t auctionId, uint8_t won,
	                                                               uint64_t finalBidCopper,
	                                                               const std::string& winnerName);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildAuctionListResponsePacket        (uint8_t error, const std::vector<AuctionListingSummary>& listings,
	                                                            uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildAuctionPostResponsePacket        (uint8_t error, uint64_t auctionId,
	                                                            uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildAuctionBidResponsePacket         (uint8_t error, uint8_t isBuyout,
	                                                            uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildAuctionCancelResponsePacket      (uint8_t error,
	                                                            uint32_t requestId, uint64_t sessionIdHeader);

	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildAuctionExpiredNotificationPacket (uint64_t auctionId, uint8_t won,
	                                                            uint64_t finalBidCopper,
	                                                            const std::string& winnerName,
	                                                            uint64_t sessionIdHeader);
}
