#pragma once

/// M35.4 — Auction house runtime: listings, bids, buyout, expiry cron, AH cut.
///
/// Responsibilities:
///  - Hold the in-memory listing table (authoritative for single-shard MVP).
///  - PostListing: escrow item from seller inventory, insert listing.
///  - PlaceBid: validate min 5% increment, update high_bidder, notify outbid.
///  - Buyout: end auction instantly, transfer item → buyer, gold → seller (minus 5%).
///  - Tick: called by TickScheduler; expires auctions and queues deliveries.
///  - SearchListings: filter by item_id / max_price, sort, paginate.
///  - DeliverPending: push queued deliveries to a client on login.
///
/// Dependency chain (M35.4):
///  - PlayerWalletService (M35.1) — gold debit/credit for bids and proceeds.
///  - ConnectedClient (ServerApp.h) — inventory mutation for escrow.
///  - ServerProtocol.h — AH message structs (M35.4).

#include "engine/server/PlayerWalletService.h"
#include "engine/server/ServerProtocol.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::server
{
	struct ConnectedClient;

	/// Status of one auction listing (runtime only; mirrored to DB string on flush).
	enum class AHListingStatus : uint8_t
	{
		Active    = 0, ///< Accepting bids and buyouts.
		Sold      = 1, ///< Bought out or won at expiry.
		Expired   = 2, ///< Expired with no bids; item returned to seller.
		Cancelled = 3  ///< Seller cancelled before any bid.
	};

	/// One pending delivery queued for a player that may be offline (M35.4).
	struct AHPendingDelivery
	{
		uint32_t    recipientClientId = 0; ///< Runtime client id (0 when offline target).
		uint64_t    recipientCharId   = 0; ///< characters.id for persistence.
		uint64_t    goldAmount        = 0; ///< Gold to credit; 0 when item-only.
		uint32_t    itemId            = 0; ///< Item to deliver; 0 when gold-only.
		uint32_t    itemQuantity      = 0;
		std::string reason;                ///< "sold"|"outbid"|"expired_no_bid"|"cancelled"
	};

	/// In-memory representation of one auction listing (M35.4).
	struct AHListing
	{
		uint64_t   listingId      = 0;
		uint32_t   sellerClientId = 0; ///< Runtime client id of the seller.
		uint32_t   itemId         = 0;
		uint32_t   itemQuantity   = 1;
		uint64_t   startBid       = 0;
		uint64_t   buyout         = 0;  ///< 0 = no buyout price.
		uint64_t   currentBid     = 0;
		uint32_t   highBidderClientId = 0; ///< 0 = no bid yet.
		AHListingStatus status    = AHListingStatus::Active;
		std::chrono::steady_clock::time_point expiresAt;
	};

	/// AH fee fraction applied to seller proceeds (5 %).
	inline constexpr uint64_t kAHFeeNumerator   = 5u;
	inline constexpr uint64_t kAHFeeDenominator = 100u;

	/// Minimum bid increment as a fraction of the current bid (5 %).
	inline constexpr uint64_t kAHMinBidIncrementNumerator   = 5u;
	inline constexpr uint64_t kAHMinBidIncrementDenominator = 100u;

	/// Maximum active listings per seller enforced in the MVP.
	inline constexpr uint32_t kAHMaxListingsPerSeller = 50u;

	/// Page size for AHSearchResult (number of entries per response).
	inline constexpr uint32_t kAHSearchPageSize = 20u;

	/// Server-side auction house runtime (M35.4).
	///
	/// All methods are called from the single authoritative game-shard tick thread.
	/// No internal synchronisation is needed.
	class AuctionHouseRuntime final
	{
	public:
		/// Construct the runtime; \p walletService lifetime must exceed this object.
		explicit AuctionHouseRuntime(PlayerWalletService& walletService);

		/// Release listings and emit shutdown log.
		~AuctionHouseRuntime();

		/// Initialise the runtime (sets up state, logs init).
		/// Returns true on success.
		bool Init();

		/// Release all listings and pending deliveries; emit shutdown log.
		void Shutdown();

		/// Post a new auction listing on behalf of \p seller.
		/// Removes the item from the seller's inventory (escrow).
		/// Populates \p outResult with the outcome.
		void PostListing(
			ConnectedClient&            seller,
			const AHPostListingMessage& request,
			AHPostListingResultMessage& outResult);

		/// Search active listings with filters from \p request.
		/// Populates \p outResult with the paginated results.
		void SearchListings(
			const AHSearchRequestMessage& request,
			AHSearchResultMessage&        outResult) const;

		/// Return the caller's own active listings.
		void GetMyListings(
			uint32_t                   clientId,
			AHMyListingsResultMessage& outResult) const;

		/// Place a bid on listing \p listingId on behalf of \p bidder.
		/// On outbid, queues a pending delivery to the previous high bidder.
		/// Populates \p outResult with the outcome.
		void PlaceBid(
			ConnectedClient&     bidder,
			const AHBidMessage&  request,
			AHBidResultMessage&  outResult);

		/// Instant buyout of listing \p listingId by \p buyer.
		/// Ends the auction, transfers item → buyer, gold → seller (minus 5 % cut).
		/// Queues outbid delivery to previous high bidder if any.
		void Buyout(
			ConnectedClient&        buyer,
			const AHBuyoutMessage&  request,
			AHBuyoutResultMessage&  outResult);

		/// Cancel one of the seller's own listings (no bids placed yet required).
		/// Returns escrowed item to the seller.
		void CancelListing(
			ConnectedClient&             seller,
			const AHCancelListingMessage& request,
			AHCancelResultMessage&        outResult);

		/// Called every server tick; expires listings whose time has elapsed.
		/// Queues pending deliveries for buyers, sellers, and outbid players.
		/// \p connectedClients is the live client table used for online delivery.
		void Tick(std::vector<ConnectedClient>& connectedClients);

		/// Flush all undelivered pending deliveries to \p client on login.
		/// Applies gold credits and item additions directly to the client.
		void DeliverPending(ConnectedClient& client);

		/// Return the full pending delivery queue (for diagnostics / persistence).
		const std::vector<AHPendingDelivery>& GetPendingDeliveries() const
		{
			return m_pendingDeliveries;
		}

	private:
		/// Find a listing by id; returns nullptr when not found.
		AHListing* FindListing(uint64_t listingId);

		/// Compute the seller net proceeds: total − 5% AH cut (floored).
		static uint64_t ComputeSellerProceeds(uint64_t grossAmount);

		/// Compute the minimum next valid bid given the current bid.
		/// When currentBid == 0 the start bid already serves as minimum.
		static uint64_t ComputeMinNextBid(uint64_t currentBid, uint64_t startBid);

		/// Convert listing to an AHListingEntry for wire serialisation.
		static AHListingEntry ToWireEntry(const AHListing& listing);

		/// Remove one item stack from client inventory (escrow on post).
		/// Returns false when the item or quantity is insufficient.
		static bool EscrowItem(ConnectedClient& client, uint32_t itemId, uint32_t quantity);

		/// Return one item stack to client inventory.
		static void ReturnItem(ConnectedClient& client, uint32_t itemId, uint32_t quantity);

		/// Queue a delivery for a potentially-offline player.
		void QueueDelivery(
			uint32_t    recipientClientId,
			uint64_t    goldAmount,
			uint32_t    itemId,
			uint32_t    itemQuantity,
			const char* reason);

		/// Apply one pending delivery immediately to an online client.
		bool ApplyDelivery(ConnectedClient& client, const AHPendingDelivery& delivery);

		PlayerWalletService&           m_walletService;
		std::vector<AHListing>         m_listings;
		std::vector<AHPendingDelivery> m_pendingDeliveries;
		uint64_t                       m_nextListingId = 1u;
		bool                           m_initialized   = false;
	};
}
