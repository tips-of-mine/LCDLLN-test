#include "engine/server/AuctionHouse.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>

namespace engine::server
{
	namespace
	{
		uint32_t DisplayPrice(const AuctionListingRecord& r)
		{
			return r.currentBid > 0u ? r.currentBid : r.startBid;
		}
	}

	AuctionHouseService::AuctionHouseService(const engine::core::Config& config)
		: m_config(config)
		, m_relativeListingsPath(m_config.GetString("server.auction_listings_path", "auction/listings.ini"))
		, m_relativeHistoryPath(m_config.GetString("server.auction_history_path", "auction/history.log"))
	{
		LOG_INFO(Net, "[AuctionHouse] Constructed (listings_path={})", m_relativeListingsPath);
	}

	AuctionHouseService::~AuctionHouseService()
	{
		Shutdown();
	}

	bool AuctionHouseService::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[AuctionHouse] Init ignored: already initialized");
			return true;
		}

		if (!LoadFromContent())
		{
			LOG_WARN(Net, "[AuctionHouse] Load produced empty store (path={})", m_relativeListingsPath);
			m_listings.clear();
			m_nextListingId = 1;
		}

		m_initialized = true;
		LOG_INFO(Net, "[AuctionHouse] Init OK (listings={}, next_id={})", m_listings.size(), m_nextListingId);
		return true;
	}

	void AuctionHouseService::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		if (!SaveToContent())
		{
			LOG_WARN(Net, "[AuctionHouse] Save FAILED on shutdown (path={})", m_relativeListingsPath);
		}
		else
		{
			LOG_INFO(Net, "[AuctionHouse] Saved on shutdown (listings={})", m_listings.size());
		}

		m_initialized = false;
		m_listings.clear();
		m_nextListingId = 1;
		LOG_INFO(Net, "[AuctionHouse] Destroyed");
	}

	bool AuctionHouseService::LoadFromContent()
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(m_config, m_relativeListingsPath);
		if (text.empty())
		{
			LOG_INFO(Net, "[AuctionHouse] No listings file yet (path={}) — starting empty", m_relativeListingsPath);
			return false;
		}

		engine::core::Config ini;
		const std::string resolved = engine::platform::FileSystem::ResolveContentPath(m_config, m_relativeListingsPath).string();
		if (!ini.LoadFromFile(resolved))
		{
			LOG_WARN(Net, "[AuctionHouse] Parse FAILED (path={})", m_relativeListingsPath);
			return false;
		}

		m_listings.clear();
		m_nextListingId = static_cast<uint32_t>(std::max<int64_t>(1, ini.GetInt("auction.next_listing_id", 1)));
		const uint32_t count = static_cast<uint32_t>(std::max<int64_t>(0, ini.GetInt("auction.listing_count", 0)));
		const uint32_t maxRows = std::min<uint32_t>(count, 4096u);
		for (uint32_t i = 0; i < maxRows; ++i)
		{
			const std::string p = "auction.l" + std::to_string(i) + ".";
			const uint32_t id = static_cast<uint32_t>(ini.GetInt(p + "id", 0));
			if (id == 0u)
			{
				continue;
			}
			AuctionListingRecord r{};
			r.listingId = id;
			r.sellerClientId = static_cast<uint32_t>(ini.GetInt(p + "seller_client", 0));
			r.sellerCharacterKey = static_cast<uint32_t>(ini.GetInt(p + "seller_ck", 0));
			r.itemId = static_cast<uint32_t>(ini.GetInt(p + "item", 0));
			r.quantity = static_cast<uint32_t>(ini.GetInt(p + "qty", 0));
			r.startBid = static_cast<uint32_t>(ini.GetInt(p + "start", 0));
			r.buyoutPrice = static_cast<uint32_t>(ini.GetInt(p + "buyout", 0));
			r.currentBid = static_cast<uint32_t>(ini.GetInt(p + "cur", 0));
			r.highBidderClientId = static_cast<uint32_t>(ini.GetInt(p + "high_client", 0));
			r.highBidderCharacterKey = static_cast<uint32_t>(ini.GetInt(p + "high_ck", 0));
			r.expiresAtTick = static_cast<uint32_t>(ini.GetInt(p + "exp_tick", 0));
			r.closed = ini.GetInt(p + "closed", 0) != 0;
			m_listings.push_back(r);
			m_nextListingId = std::max(m_nextListingId, id + 1u);
		}

		LOG_INFO(Net, "[AuctionHouse] Load OK (rows={}, next_id={})", m_listings.size(), m_nextListingId);
		return true;
	}

	bool AuctionHouseService::SaveToContent() const
	{
		std::ostringstream os;
		os << "auction.next_listing_id=" << m_nextListingId << "\n";
		uint32_t saved = 0;
		for (const AuctionListingRecord& r : m_listings)
		{
			if (r.closed)
			{
				continue;
			}
			const std::string p = "auction.l" + std::to_string(saved) + ".";
			os << p << "id=" << r.listingId << "\n";
			os << p << "seller_client=" << r.sellerClientId << "\n";
			os << p << "seller_ck=" << r.sellerCharacterKey << "\n";
			os << p << "item=" << r.itemId << "\n";
			os << p << "qty=" << r.quantity << "\n";
			os << p << "start=" << r.startBid << "\n";
			os << p << "buyout=" << r.buyoutPrice << "\n";
			os << p << "cur=" << r.currentBid << "\n";
			os << p << "high_client=" << r.highBidderClientId << "\n";
			os << p << "high_ck=" << r.highBidderCharacterKey << "\n";
			os << p << "exp_tick=" << r.expiresAtTick << "\n";
			os << p << "closed=0\n";
			++saved;
		}
		os << "auction.listing_count=" << saved << "\n";

		if (!engine::platform::FileSystem::WriteAllTextContent(m_config, m_relativeListingsPath, os.str()))
		{
			LOG_ERROR(Net, "[AuctionHouse] Write FAILED (path={})", m_relativeListingsPath);
			return false;
		}
		LOG_INFO(Net, "[AuctionHouse] Save OK (active_listings={})", saved);
		return true;
	}

	bool AuctionHouseService::AppendHistoryLine(std::string_view line) const
	{
		const auto full = engine::platform::FileSystem::ResolveContentPath(m_config, m_relativeHistoryPath);
		std::ofstream f(full, std::ios::app | std::ios::binary);
		if (!f)
		{
			LOG_WARN(Net, "[AuctionHouse] History append FAILED (path={})", m_relativeHistoryPath);
			return false;
		}
		f.write(line.data(), static_cast<std::streamsize>(line.size()));
		f.put('\n');
		LOG_INFO(Net, "[AuctionHouse] History line appended");
		return true;
	}

	AuctionListingRecord* AuctionHouseService::FindListing(uint32_t listingId)
	{
		for (AuctionListingRecord& r : m_listings)
		{
			if (!r.closed && r.listingId == listingId)
			{
				return &r;
			}
		}
		return nullptr;
	}

	uint32_t AuctionHouseService::MinimumNextBid(const AuctionListingRecord& row) const
	{
		if (row.currentBid == 0u)
		{
			return row.startBid;
		}
		const uint32_t inc = std::max(1u, (row.currentBid * 5u) / 100u);
		const uint64_t sum = static_cast<uint64_t>(row.currentBid) + static_cast<uint64_t>(inc);
		if (sum > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
		{
			return std::numeric_limits<uint32_t>::max();
		}
		return static_cast<uint32_t>(sum);
	}

	bool AuctionHouseService::ValidateNewListingParams(
		uint32_t startBid,
		uint32_t buyoutPrice,
		uint8_t durationHours,
		std::string& outError) const
	{
		if (startBid == 0u)
		{
			outError = "start_bid_zero";
			return false;
		}
		if (durationHours != 12u && durationHours != 24u && durationHours != 48u)
		{
			outError = "bad_duration";
			return false;
		}
		if (buyoutPrice != 0u && buyoutPrice < startBid)
		{
			outError = "buyout_below_start";
			return false;
		}
		(void)outError;
		return true;
	}

	uint32_t AuctionHouseService::EmplaceListing(AuctionListingRecord row)
	{
		row.listingId = m_nextListingId++;
		m_listings.push_back(row);
		(void)SaveToContent();
		LOG_INFO(Net, "[AuctionHouse] Listing created (id={}, item={}, qty={})", row.listingId, row.itemId, row.quantity);
		return row.listingId;
	}

	void AuctionHouseService::MarkClosed(uint32_t listingId)
	{
		for (AuctionListingRecord& r : m_listings)
		{
			if (r.listingId == listingId)
			{
				r.closed = true;
				break;
			}
		}
	}

	void AuctionHouseService::EraseListing(uint32_t listingId)
	{
		const auto it = std::remove_if(
			m_listings.begin(),
			m_listings.end(),
			[listingId](const AuctionListingRecord& r) { return r.listingId == listingId; });
		if (it != m_listings.end())
		{
			m_listings.erase(it, m_listings.end());
			(void)SaveToContent();
			LOG_INFO(Net, "[AuctionHouse] Listing erased (id={})", listingId);
		}
	}

	std::vector<const AuctionListingRecord*> AuctionHouseService::QueryBrowse(
		uint32_t minPrice,
		uint32_t maxPrice,
		uint32_t itemIdFilter,
		uint8_t sortMode,
		uint32_t maxRows) const
	{
		std::vector<const AuctionListingRecord*> rows;
		for (const AuctionListingRecord& r : m_listings)
		{
			if (r.closed)
			{
				continue;
			}
			if (itemIdFilter != 0u && r.itemId != itemIdFilter)
			{
				continue;
			}
			const uint32_t disp = DisplayPrice(r);
			if (disp < minPrice)
			{
				continue;
			}
			if (maxPrice != 0u && disp > maxPrice)
			{
				continue;
			}
			rows.push_back(&r);
		}

		if (sortMode == 0u)
		{
			std::sort(rows.begin(), rows.end(), [](const AuctionListingRecord* a, const AuctionListingRecord* b) {
				return DisplayPrice(*a) < DisplayPrice(*b);
			});
		}
		else if (sortMode == 1u)
		{
			std::sort(rows.begin(), rows.end(), [](const AuctionListingRecord* a, const AuctionListingRecord* b) {
				return DisplayPrice(*a) > DisplayPrice(*b);
			});
		}
		else
		{
			std::sort(rows.begin(), rows.end(), [](const AuctionListingRecord* a, const AuctionListingRecord* b) {
				return a->expiresAtTick < b->expiresAtTick;
			});
		}

		if (rows.size() > maxRows)
		{
			rows.resize(maxRows);
		}
		return rows;
	}

	void AuctionHouseService::CollectExpired(uint32_t currentTick, std::vector<AuctionSettlement>& outSettlements)
	{
		const size_t before = outSettlements.size();
		for (auto it = m_listings.begin(); it != m_listings.end();)
		{
			AuctionListingRecord& r = *it;
			if (r.closed || r.expiresAtTick == 0u || currentTick < r.expiresAtTick)
			{
				++it;
				continue;
			}

			AuctionSettlement s{};
			s.listingId = r.listingId;
			s.item = ItemStack{ r.itemId, r.quantity };
			s.sellerCharacterKey = r.sellerCharacterKey;

			if (r.currentBid == 0u || r.highBidderCharacterKey == 0u)
			{
				s.expiredWithoutBids = true;
				s.finalPrice = 0;
				s.sellerProceeds = 0;
				s.buyerCharacterKey = 0;
				s.buyerClientId = 0;
			}
			else
			{
				s.expiredWithoutBids = false;
				s.finalPrice = r.currentBid;
				s.sellerProceeds = (r.currentBid * (100u - kAuctionHouseFeePercent)) / 100u;
				s.buyerCharacterKey = r.highBidderCharacterKey;
				s.buyerClientId = r.highBidderClientId;
			}

			outSettlements.push_back(s);
			it = m_listings.erase(it);
		}

		if (outSettlements.size() > before)
		{
			(void)SaveToContent();
			LOG_INFO(Net, "[AuctionHouse] Expired settlements={}", outSettlements.size() - before);
		}
	}

	bool AuctionHouseService::PersistListings()
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[AuctionHouse] PersistListings ignored: not initialized");
			return false;
		}
		if (!SaveToContent())
		{
			LOG_ERROR(Net, "[AuctionHouse] PersistListings FAILED");
			return false;
		}
		LOG_INFO(Net, "[AuctionHouse] PersistListings OK");
		return true;
	}
}
