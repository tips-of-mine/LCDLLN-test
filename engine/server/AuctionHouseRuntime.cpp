#include "engine/server/AuctionHouseRuntime.h"

#include "engine/server/ServerApp.h" // ConnectedClient full definition
#include "engine/core/Log.h"

#include <algorithm>
#include <chrono>

namespace engine::server
{
	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	namespace
	{
		/// Return seconds until \p expiresAt from now, clamped to 0.
		uint32_t SecondsUntil(std::chrono::steady_clock::time_point expiresAt)
		{
			using namespace std::chrono;
			const auto now = steady_clock::now();
			if (expiresAt <= now)
			{
				return 0u;
			}
			const auto secs = duration_cast<seconds>(expiresAt - now).count();
			return secs > 0xFFFFFFFFLL ? 0xFFFFFFFFu : static_cast<uint32_t>(secs);
		}
	}

	// -------------------------------------------------------------------------
	// Static helpers
	// -------------------------------------------------------------------------

	/* static */
	uint64_t AuctionHouseRuntime::ComputeSellerProceeds(uint64_t grossAmount)
	{
		const uint64_t cut = (grossAmount * kAHFeeNumerator) / kAHFeeDenominator;
		return (grossAmount > cut) ? (grossAmount - cut) : 0u;
	}

	/* static */
	uint64_t AuctionHouseRuntime::ComputeMinNextBid(uint64_t currentBid, uint64_t startBid)
	{
		if (currentBid == 0u)
		{
			return startBid;
		}
		const uint64_t increment = std::max(uint64_t{1},
			(currentBid * kAHMinBidIncrementNumerator) / kAHMinBidIncrementDenominator);
		return currentBid + increment;
	}

	/* static */
	AHListingEntry AuctionHouseRuntime::ToWireEntry(const AHListing& listing)
	{
		AHListingEntry e{};
		e.listingId    = listing.listingId;
		e.sellerItemId = listing.itemId;
		e.itemQuantity = listing.itemQuantity;
		e.startBid     = listing.startBid;
		e.buyout       = listing.buyout;
		e.currentBid   = listing.currentBid;
		e.expiresInSec = SecondsUntil(listing.expiresAt);
		e.hasBid       = (listing.highBidderClientId != 0u) ? 1u : 0u;
		return e;
	}

	/* static */
	bool AuctionHouseRuntime::EscrowItem(ConnectedClient& client, uint32_t itemId, uint32_t quantity)
	{
		for (auto it = client.inventory.begin(); it != client.inventory.end(); ++it)
		{
			if (it->itemId == itemId && it->quantity >= quantity)
			{
				it->quantity -= quantity;
				if (it->quantity == 0u)
				{
					client.inventory.erase(it);
				}
				return true;
			}
		}
		return false;
	}

	/* static */
	void AuctionHouseRuntime::ReturnItem(ConnectedClient& client, uint32_t itemId, uint32_t quantity)
	{
		for (ItemStack& stack : client.inventory)
		{
			if (stack.itemId == itemId)
			{
				stack.quantity += quantity;
				return;
			}
		}
		client.inventory.push_back({itemId, quantity});
	}

	// -------------------------------------------------------------------------
	// AuctionHouseRuntime
	// -------------------------------------------------------------------------

	AuctionHouseRuntime::AuctionHouseRuntime(PlayerWalletService& walletService)
		: m_walletService(walletService)
	{
	}

	AuctionHouseRuntime::~AuctionHouseRuntime()
	{
		Shutdown();
	}

	bool AuctionHouseRuntime::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[AuctionHouseRuntime] Init ignored: already initialized");
			return true;
		}
		m_nextListingId = 1u;
		m_listings.clear();
		m_pendingDeliveries.clear();
		m_initialized = true;
		LOG_INFO(Net, "[AuctionHouseRuntime] Init OK");
		return true;
	}

	void AuctionHouseRuntime::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_listings.clear();
		m_pendingDeliveries.clear();
		m_initialized = false;
		LOG_INFO(Net, "[AuctionHouseRuntime] Destroyed");
	}

	// -------------------------------------------------------------------------
	// PostListing
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::PostListing(
		ConnectedClient&            seller,
		const AHPostListingMessage& request,
		AHPostListingResultMessage& outResult)
	{
		outResult.clientId  = seller.clientId;
		outResult.success   = 0u;
		outResult.listingId = 0u;

		if (!m_initialized)
		{
			outResult.errorReason = "ah_not_ready";
			LOG_WARN(Net, "[AuctionHouseRuntime] PostListing FAILED: not initialized (client={})", seller.clientId);
			return;
		}

		// Validate request fields.
		if (request.itemId == 0u || request.itemQuantity == 0u)
		{
			outResult.errorReason = "invalid_item";
			LOG_WARN(Net, "[AuctionHouseRuntime] PostListing FAILED: invalid item (client={})", seller.clientId);
			return;
		}
		if (request.startBid == 0u)
		{
			outResult.errorReason = "invalid_start_bid";
			LOG_WARN(Net, "[AuctionHouseRuntime] PostListing FAILED: start_bid=0 (client={})", seller.clientId);
			return;
		}
		if (request.buyout != 0u && request.buyout < request.startBid)
		{
			outResult.errorReason = "buyout_below_start_bid";
			LOG_WARN(Net, "[AuctionHouseRuntime] PostListing FAILED: buyout < start_bid (client={})", seller.clientId);
			return;
		}

		// Enforce per-seller listing cap.
		uint32_t sellerActive = 0u;
		for (const AHListing& l : m_listings)
		{
			if (l.sellerClientId == seller.clientId && l.status == AHListingStatus::Active)
			{
				++sellerActive;
			}
		}
		if (sellerActive >= kAHMaxListingsPerSeller)
		{
			outResult.errorReason = "listing_cap_reached";
			LOG_WARN(Net, "[AuctionHouseRuntime] PostListing FAILED: cap reached (client={}, active={})",
				seller.clientId, sellerActive);
			return;
		}

		// Validate duration.
		const uint8_t durationHours = static_cast<uint8_t>(request.duration);
		if (durationHours != 12u && durationHours != 24u && durationHours != 48u)
		{
			outResult.errorReason = "invalid_duration";
			LOG_WARN(Net, "[AuctionHouseRuntime] PostListing FAILED: bad duration={} (client={})",
				durationHours, seller.clientId);
			return;
		}

		// Escrow item from seller inventory.
		if (!EscrowItem(seller, request.itemId, request.itemQuantity))
		{
			outResult.errorReason = "insufficient_item";
			LOG_WARN(Net, "[AuctionHouseRuntime] PostListing FAILED: escrow failed (client={}, item={}, qty={})",
				seller.clientId, request.itemId, request.itemQuantity);
			return;
		}

		// Create listing.
		const uint64_t listingId = m_nextListingId++;
		AHListing listing{};
		listing.listingId         = listingId;
		listing.sellerClientId    = seller.clientId;
		listing.itemId            = request.itemId;
		listing.itemQuantity      = request.itemQuantity;
		listing.startBid          = request.startBid;
		listing.buyout            = request.buyout;
		listing.currentBid        = 0u;
		listing.highBidderClientId = 0u;
		listing.status            = AHListingStatus::Active;
		listing.expiresAt         = std::chrono::steady_clock::now()
		                            + std::chrono::hours(durationHours);

		m_listings.push_back(listing);

		outResult.success   = 1u;
		outResult.listingId = listingId;
		LOG_INFO(Net,
			"[AuctionHouseRuntime] PostListing OK (client={}, listing={}, item={}, qty={}, start={}, buyout={}, duration={}h)",
			seller.clientId, listingId, request.itemId, request.itemQuantity,
			request.startBid, request.buyout, durationHours);
	}

	// -------------------------------------------------------------------------
	// SearchListings
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::SearchListings(
		const AHSearchRequestMessage& request,
		AHSearchResultMessage&        outResult) const
	{
		outResult.clientId   = request.clientId;
		outResult.pageIndex  = request.pageIndex;

		// Collect matching active listings.
		std::vector<const AHListing*> matches;
		matches.reserve(m_listings.size());
		for (const AHListing& listing : m_listings)
		{
			if (listing.status != AHListingStatus::Active)
			{
				continue;
			}
			if (request.itemId != 0u && listing.itemId != request.itemId)
			{
				continue;
			}
			// Filter by effective price (current_bid when bid exists, else start_bid).
			const uint64_t effectivePrice = (listing.currentBid > 0u)
				? listing.currentBid : listing.startBid;
			if (request.maxPrice != 0u && effectivePrice > request.maxPrice)
			{
				continue;
			}
			matches.push_back(&listing);
		}

		// Sort.
		auto sortFn = [&](const AHListing* a, const AHListing* b) -> bool
		{
			const uint64_t priceA = (a->currentBid > 0u) ? a->currentBid : a->startBid;
			const uint64_t priceB = (b->currentBid > 0u) ? b->currentBid : b->startBid;
			const auto     timeA  = a->expiresAt;
			const auto     timeB  = b->expiresAt;
			switch (request.sortOrder)
			{
			case AHSortOrder::PriceAsc:  return priceA < priceB;
			case AHSortOrder::PriceDesc: return priceA > priceB;
			case AHSortOrder::TimeAsc:   return timeA < timeB;
			case AHSortOrder::TimeDesc:  return timeA > timeB;
			default:                     return priceA < priceB;
			}
		};
		std::sort(matches.begin(), matches.end(), sortFn);

		outResult.totalCount = static_cast<uint32_t>(matches.size());

		// Paginate.
		const uint32_t offset = request.pageIndex * kAHSearchPageSize;
		outResult.listings.clear();
		if (offset < static_cast<uint32_t>(matches.size()))
		{
			const uint32_t end = std::min(
				offset + kAHSearchPageSize,
				static_cast<uint32_t>(matches.size()));
			for (uint32_t i = offset; i < end; ++i)
			{
				outResult.listings.push_back(ToWireEntry(*matches[i]));
			}
		}

		LOG_DEBUG(Net,
			"[AuctionHouseRuntime] Search (client={}, item={}, maxPrice={}, page={}) → total={} returned={}",
			request.clientId, request.itemId, request.maxPrice,
			request.pageIndex, outResult.totalCount, outResult.listings.size());
	}

	// -------------------------------------------------------------------------
	// GetMyListings
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::GetMyListings(
		uint32_t                   clientId,
		AHMyListingsResultMessage& outResult) const
	{
		outResult.clientId = clientId;
		outResult.listings.clear();
		for (const AHListing& listing : m_listings)
		{
			if (listing.sellerClientId == clientId && listing.status == AHListingStatus::Active)
			{
				outResult.listings.push_back(ToWireEntry(listing));
			}
		}
		LOG_DEBUG(Net,
			"[AuctionHouseRuntime] GetMyListings (client={}) → {} active listings",
			clientId, outResult.listings.size());
	}

	// -------------------------------------------------------------------------
	// PlaceBid
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::PlaceBid(
		ConnectedClient&    bidder,
		const AHBidMessage& request,
		AHBidResultMessage& outResult)
	{
		outResult.clientId  = bidder.clientId;
		outResult.listingId = request.listingId;
		outResult.success   = 0u;
		outResult.newBid    = 0u;

		if (!m_initialized)
		{
			outResult.errorReason = "ah_not_ready";
			return;
		}

		AHListing* listing = FindListing(request.listingId);
		if (listing == nullptr || listing->status != AHListingStatus::Active)
		{
			outResult.errorReason = "listing_not_found";
			LOG_WARN(Net, "[AuctionHouseRuntime] PlaceBid FAILED: listing not found (id={}, client={})",
				request.listingId, bidder.clientId);
			return;
		}

		// Prevent seller from bidding on own auction.
		if (listing->sellerClientId == bidder.clientId)
		{
			outResult.errorReason = "cannot_bid_own_auction";
			LOG_WARN(Net, "[AuctionHouseRuntime] PlaceBid FAILED: self-bid (listing={}, client={})",
				request.listingId, bidder.clientId);
			return;
		}

		// Validate minimum bid increment.
		const uint64_t minBid = ComputeMinNextBid(listing->currentBid, listing->startBid);
		if (request.bidAmount < minBid)
		{
			outResult.errorReason = "bid_too_low";
			LOG_WARN(Net,
				"[AuctionHouseRuntime] PlaceBid FAILED: bid_too_low (listing={}, offered={}, min={})",
				request.listingId, request.bidAmount, minBid);
			return;
		}

		// Deduct gold from bidder.
		std::string walletErr;
		if (!m_walletService.SubtractCurrency(bidder, kCurrencyGold, request.bidAmount, walletErr))
		{
			outResult.errorReason = walletErr;
			LOG_WARN(Net, "[AuctionHouseRuntime] PlaceBid FAILED: wallet debit (listing={}, client={}, err={})",
				request.listingId, bidder.clientId, walletErr);
			return;
		}

		// Return gold to the previous high bidder (outbid).
		if (listing->highBidderClientId != 0u && listing->currentBid > 0u)
		{
			QueueDelivery(listing->highBidderClientId, listing->currentBid, 0u, 0u, "outbid");
			LOG_INFO(Net,
				"[AuctionHouseRuntime] Outbid: queued gold return to client={} amount={} (listing={})",
				listing->highBidderClientId, listing->currentBid, listing->listingId);
		}

		// Update listing.
		listing->currentBid         = request.bidAmount;
		listing->highBidderClientId = bidder.clientId;

		outResult.success = 1u;
		outResult.newBid  = request.bidAmount;
		LOG_INFO(Net,
			"[AuctionHouseRuntime] PlaceBid OK (listing={}, client={}, amount={})",
			request.listingId, bidder.clientId, request.bidAmount);
	}

	// -------------------------------------------------------------------------
	// Buyout
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::Buyout(
		ConnectedClient&        buyer,
		const AHBuyoutMessage&  request,
		AHBuyoutResultMessage&  outResult)
	{
		outResult.clientId  = buyer.clientId;
		outResult.listingId = request.listingId;
		outResult.success   = 0u;

		if (!m_initialized)
		{
			outResult.errorReason = "ah_not_ready";
			return;
		}

		AHListing* listing = FindListing(request.listingId);
		if (listing == nullptr || listing->status != AHListingStatus::Active)
		{
			outResult.errorReason = "listing_not_found";
			LOG_WARN(Net, "[AuctionHouseRuntime] Buyout FAILED: listing not found (id={}, client={})",
				request.listingId, buyer.clientId);
			return;
		}
		if (listing->buyout == 0u)
		{
			outResult.errorReason = "no_buyout_price";
			LOG_WARN(Net, "[AuctionHouseRuntime] Buyout FAILED: no buyout price (listing={}, client={})",
				request.listingId, buyer.clientId);
			return;
		}

		// Prevent seller from buying out own auction.
		if (listing->sellerClientId == buyer.clientId)
		{
			outResult.errorReason = "cannot_buyout_own_auction";
			LOG_WARN(Net, "[AuctionHouseRuntime] Buyout FAILED: self-buyout (listing={}, client={})",
				request.listingId, buyer.clientId);
			return;
		}

		// Deduct buyout gold from buyer.
		std::string walletErr;
		if (!m_walletService.SubtractCurrency(buyer, kCurrencyGold, listing->buyout, walletErr))
		{
			outResult.errorReason = walletErr;
			LOG_WARN(Net, "[AuctionHouseRuntime] Buyout FAILED: wallet debit (listing={}, client={}, err={})",
				request.listingId, buyer.clientId, walletErr);
			return;
		}

		// Return gold to previous high bidder (if any).
		if (listing->highBidderClientId != 0u && listing->currentBid > 0u)
		{
			QueueDelivery(listing->highBidderClientId, listing->currentBid, 0u, 0u, "outbid");
		}

		// Deliver item to buyer.
		QueueDelivery(buyer.clientId, 0u, listing->itemId, listing->itemQuantity, "sold");

		// Credit seller net proceeds (buyout minus 5% AH cut).
		const uint64_t sellerProceeds = ComputeSellerProceeds(listing->buyout);
		QueueDelivery(listing->sellerClientId, sellerProceeds, 0u, 0u, "sold");

		listing->status = AHListingStatus::Sold;

		outResult.success = 1u;
		LOG_INFO(Net,
			"[AuctionHouseRuntime] Buyout OK (listing={}, buyer={}, price={}, seller_net={})",
			listing->listingId, buyer.clientId, listing->buyout, sellerProceeds);
	}

	// -------------------------------------------------------------------------
	// CancelListing
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::CancelListing(
		ConnectedClient&              seller,
		const AHCancelListingMessage& request,
		AHCancelResultMessage&        outResult)
	{
		outResult.clientId  = seller.clientId;
		outResult.listingId = request.listingId;
		outResult.success   = 0u;

		if (!m_initialized)
		{
			outResult.errorReason = "ah_not_ready";
			return;
		}

		AHListing* listing = FindListing(request.listingId);
		if (listing == nullptr || listing->status != AHListingStatus::Active)
		{
			outResult.errorReason = "listing_not_found";
			LOG_WARN(Net, "[AuctionHouseRuntime] CancelListing FAILED: not found (id={}, client={})",
				request.listingId, seller.clientId);
			return;
		}
		if (listing->sellerClientId != seller.clientId)
		{
			outResult.errorReason = "not_your_listing";
			LOG_WARN(Net, "[AuctionHouseRuntime] CancelListing FAILED: not owner (listing={}, client={})",
				request.listingId, seller.clientId);
			return;
		}
		if (listing->highBidderClientId != 0u)
		{
			outResult.errorReason = "bid_already_placed";
			LOG_WARN(Net, "[AuctionHouseRuntime] CancelListing FAILED: bid already placed (listing={})",
				request.listingId);
			return;
		}

		// Return escrowed item to seller.
		ReturnItem(seller, listing->itemId, listing->itemQuantity);
		listing->status = AHListingStatus::Cancelled;

		outResult.success = 1u;
		LOG_INFO(Net,
			"[AuctionHouseRuntime] CancelListing OK (listing={}, client={}, item={})",
			listing->listingId, seller.clientId, listing->itemId);
	}

	// -------------------------------------------------------------------------
	// Tick — expiry cron
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::Tick(std::vector<ConnectedClient>& connectedClients)
	{
		if (!m_initialized)
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now();

		for (AHListing& listing : m_listings)
		{
			if (listing.status != AHListingStatus::Active)
			{
				continue;
			}
			if (listing.expiresAt > now)
			{
				continue;
			}

			// Auction expired.
			if (listing.highBidderClientId != 0u && listing.currentBid > 0u)
			{
				// Sold to highest bidder.
				listing.status = AHListingStatus::Sold;

				// Deliver item to buyer.
				QueueDelivery(listing.highBidderClientId, 0u,
					listing.itemId, listing.itemQuantity, "sold");

				// Credit seller net proceeds (bid minus 5% AH cut).
				const uint64_t sellerProceeds = ComputeSellerProceeds(listing.currentBid);
				QueueDelivery(listing.sellerClientId, sellerProceeds, 0u, 0u, "sold");

				LOG_INFO(Net,
					"[AuctionHouseRuntime] Expiry: sold (listing={}, buyer={}, seller={}, net={})",
					listing.listingId, listing.highBidderClientId,
					listing.sellerClientId, sellerProceeds);
			}
			else
			{
				// No bids — return item to seller.
				listing.status = AHListingStatus::Expired;
				QueueDelivery(listing.sellerClientId, 0u,
					listing.itemId, listing.itemQuantity, "expired_no_bid");

				LOG_INFO(Net,
					"[AuctionHouseRuntime] Expiry: no bid (listing={}, seller={})",
					listing.listingId, listing.sellerClientId);
			}
		}

		// Flush deliveries to online clients.
		for (ConnectedClient& client : connectedClients)
		{
			DeliverPending(client);
		}
	}

	// -------------------------------------------------------------------------
	// DeliverPending
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::DeliverPending(ConnectedClient& client)
	{
		bool anyDelivered = false;
		for (AHPendingDelivery& delivery : m_pendingDeliveries)
		{
			if (delivery.recipientClientId != client.clientId)
			{
				continue;
			}
			if (!ApplyDelivery(client, delivery))
			{
				continue;
			}
			// Mark delivered by zeroing the recipient so the cleanup pass skips it.
			delivery.recipientClientId = 0u;
			anyDelivered = true;
		}

		if (anyDelivered)
		{
			// Remove delivered entries.
			m_pendingDeliveries.erase(
				std::remove_if(m_pendingDeliveries.begin(), m_pendingDeliveries.end(),
					[](const AHPendingDelivery& d) { return d.recipientClientId == 0u; }),
				m_pendingDeliveries.end());
		}
	}

	// -------------------------------------------------------------------------
	// FindListing
	// -------------------------------------------------------------------------

	AHListing* AuctionHouseRuntime::FindListing(uint64_t listingId)
	{
		for (AHListing& listing : m_listings)
		{
			if (listing.listingId == listingId)
			{
				return &listing;
			}
		}
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// QueueDelivery
	// -------------------------------------------------------------------------

	void AuctionHouseRuntime::QueueDelivery(
		uint32_t    recipientClientId,
		uint64_t    goldAmount,
		uint32_t    itemId,
		uint32_t    itemQuantity,
		const char* reason)
	{
		AHPendingDelivery delivery{};
		delivery.recipientClientId = recipientClientId;
		delivery.goldAmount        = goldAmount;
		delivery.itemId            = itemId;
		delivery.itemQuantity      = itemQuantity;
		delivery.reason            = reason;
		m_pendingDeliveries.push_back(delivery);
		LOG_DEBUG(Net,
			"[AuctionHouseRuntime] QueueDelivery (recipient={}, gold={}, item={}, qty={}, reason={})",
			recipientClientId, goldAmount, itemId, itemQuantity, reason);
	}

	// -------------------------------------------------------------------------
	// ApplyDelivery
	// -------------------------------------------------------------------------

	bool AuctionHouseRuntime::ApplyDelivery(ConnectedClient& client, const AHPendingDelivery& delivery)
	{
		// Credit gold if any.
		if (delivery.goldAmount > 0u)
		{
			std::string err;
			if (!m_walletService.AddCurrency(client, kCurrencyGold, delivery.goldAmount, err))
			{
				LOG_ERROR(Net,
					"[AuctionHouseRuntime] ApplyDelivery: gold credit FAILED (client={}, amount={}, err={})",
					client.clientId, delivery.goldAmount, err);
				return false;
			}
		}

		// Return item if any.
		if (delivery.itemId != 0u && delivery.itemQuantity > 0u)
		{
			ReturnItem(client, delivery.itemId, delivery.itemQuantity);
		}

		LOG_INFO(Net,
			"[AuctionHouseRuntime] Delivered (client={}, gold={}, item={}, qty={}, reason={})",
			client.clientId, delivery.goldAmount, delivery.itemId,
			delivery.itemQuantity, delivery.reason);
		return true;
	}
}
