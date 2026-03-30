#include "engine/client/AuctionHouseUi.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace engine::client
{
	namespace
	{
		/// Trim leading/trailing whitespace from a text view.
		std::string_view Trim(std::string_view value)
		{
			size_t begin = 0;
			size_t end   = value.size();
			while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
			{
				++begin;
			}
			while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
			{
				--end;
			}
			return value.substr(begin, end - begin);
		}

		/// Split a line into at most four '|'-separated columns.
		std::array<std::string_view, 4> SplitColumns(std::string_view line, size_t& outCount)
		{
			std::array<std::string_view, 4> cols{};
			outCount = 0;
			size_t start = 0;
			while (start <= line.size() && outCount < cols.size())
			{
				const size_t sep = line.find('|', start);
				if (sep == std::string_view::npos)
				{
					cols[outCount++] = line.substr(start);
					break;
				}
				cols[outCount++] = line.substr(start, sep - start);
				start = sep + 1;
			}
			return cols;
		}

		/// Compute the minimum next valid bid (5 % increment, min 1).
		uint64_t ComputeMinNextBid(uint64_t currentBid, uint64_t startBid)
		{
			if (currentBid == 0u)
			{
				return startBid;
			}
			const uint64_t increment = std::max(uint64_t{1}, (currentBid * uint64_t{5}) / uint64_t{100});
			return currentBid + increment;
		}
	}

	// -------------------------------------------------------------------------
	// AuctionHouseUiPresenter
	// -------------------------------------------------------------------------

	AuctionHouseUiPresenter::~AuctionHouseUiPresenter()
	{
		Shutdown();
	}

	bool AuctionHouseUiPresenter::Init(const engine::core::Config& config)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AuctionHouseUiPresenter] Init ignored: already initialized");
			return true;
		}

		// Reuse the shared inventory item metadata (same format as ShopUiPresenter).
		m_metadataRelPath = config.GetString("ui.inventory_items_path", "ui/inventory_items.txt");
		if (!LoadMetadata(config))
		{
			LOG_ERROR(Core, "[AuctionHouseUiPresenter] Init FAILED: metadata load failed ({})",
				m_metadataRelPath);
			return false;
		}

		m_initialized = true;
		RebuildLayout();
		RebuildSearchDebugText();
		RebuildMyListingsDebugText();
		LOG_INFO(Core, "[AuctionHouseUiPresenter] Init OK (meta_entries={}, path={})",
			m_metadata.size(), m_metadataRelPath);
		return true;
	}

	void AuctionHouseUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_initialized = false;
		m_searchState     = {};
		m_myListingsState = {};
		m_actionStatus    = {};
		m_metadata.clear();
		m_viewportWidth  = 0;
		m_viewportHeight = 0;
		m_metadataRelPath.clear();
		LOG_INFO(Core, "[AuctionHouseUiPresenter] Destroyed");
	}

	bool AuctionHouseUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AuctionHouseUiPresenter] SetViewportSize FAILED: not initialized");
			return false;
		}
		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[AuctionHouseUiPresenter] SetViewportSize FAILED: invalid size {}x{}", width, height);
			return false;
		}
		m_viewportWidth  = width;
		m_viewportHeight = height;
		RebuildLayout();
		RebuildSearchDebugText();
		RebuildMyListingsDebugText();
		LOG_INFO(Core, "[AuctionHouseUiPresenter] Viewport updated ({}x{})", width, height);
		return true;
	}

	bool AuctionHouseUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AuctionHouseUiPresenter] ApplyModel FAILED: not initialized");
			return false;
		}

		if ((changeMask & UIModelChangeAH) == 0u)
		{
			return true;
		}

		if (!m_searchState.layoutValid)
		{
			RebuildLayout();
		}

		RebuildSearchPanel(model);
		RebuildMyListingsPanel(model);

		// Mirror last action status.
		m_actionStatus.success   = model.auctionHouse.lastActionSuccess;
		m_actionStatus.listingId = model.auctionHouse.lastActionListingId;
		m_actionStatus.errorReason = model.auctionHouse.lastActionError;
		m_actionStatus.hasResult = true;

		RebuildSearchDebugText();
		RebuildMyListingsDebugText();
		LOG_DEBUG(Core, "[AuctionHouseUiPresenter] Model applied (search={}, myListings={}, open={})",
			model.auctionHouse.searchResults.size(),
			model.auctionHouse.myListings.size(),
			model.auctionHouse.isOpen ? "true" : "false");
		return true;
	}

	bool AuctionHouseUiPresenter::SelectSearchListing(uint64_t listingId)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AuctionHouseUiPresenter] SelectSearchListing FAILED: not initialized");
			return false;
		}
		m_searchState.selectedListingId = listingId;
		for (AHListingView& view : m_searchState.listings)
		{
			view.selected = (view.listingId == listingId);
		}
		RebuildSearchDebugText();
		LOG_DEBUG(Core, "[AuctionHouseUiPresenter] SelectSearchListing (id={})", listingId);
		return true;
	}

	bool AuctionHouseUiPresenter::SelectMyListing(uint64_t listingId)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AuctionHouseUiPresenter] SelectMyListing FAILED: not initialized");
			return false;
		}
		m_myListingsState.selectedListingId = listingId;
		for (AHListingView& view : m_myListingsState.listings)
		{
			view.selected = (view.listingId == listingId);
		}
		RebuildMyListingsDebugText();
		LOG_DEBUG(Core, "[AuctionHouseUiPresenter] SelectMyListing (id={})", listingId);
		return true;
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	bool AuctionHouseUiPresenter::LoadMetadata(const engine::core::Config& config)
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, m_metadataRelPath);
		if (text.empty())
		{
			LOG_WARN(Core,
				"[AuctionHouseUiPresenter] Metadata missing or empty ({}); AH will show item ids only",
				m_metadataRelPath);
			// Not a hard failure — AH is still functional without display names.
			return true;
		}

		m_metadata.clear();
		size_t lineStart = 0;
		while (lineStart <= text.size())
		{
			const size_t lineEnd = text.find('\n', lineStart);
			const std::string_view rawLine = (lineEnd == std::string::npos)
				? std::string_view(text).substr(lineStart)
				: std::string_view(text).substr(lineStart, lineEnd - lineStart);
			const std::string_view line = Trim(rawLine);
			if (!line.empty() && !line.starts_with('#'))
			{
				size_t colCount = 0;
				const auto cols = SplitColumns(line, colCount);
				if (colCount >= 3)
				{
					const std::string_view idView = Trim(cols[0]);
					uint32_t id = 0;
					bool valid = !idView.empty();
					for (const char ch : idView)
					{
						if (ch < '0' || ch > '9') { valid = false; break; }
						id = (id * 10u) + static_cast<uint32_t>(ch - '0');
					}
					if (valid)
					{
						ItemMetaEntry entry{};
						entry.itemId      = id;
						entry.iconPath    = std::string(Trim(cols[1]));
						entry.displayName = std::string(Trim(cols[2]));
						if (!entry.displayName.empty())
						{
							m_metadata.push_back(std::move(entry));
						}
					}
				}
			}
			if (lineEnd == std::string::npos)
			{
				break;
			}
			lineStart = lineEnd + 1;
		}

		LOG_INFO(Core, "[AuctionHouseUiPresenter] Metadata loaded (entries={}, path={})",
			m_metadata.size(), m_metadataRelPath);
		return true;
	}

	void AuctionHouseUiPresenter::RebuildLayout()
	{
		const float vw = static_cast<float>(m_viewportWidth  == 0 ? 1280u : m_viewportWidth);
		const float vh = static_cast<float>(m_viewportHeight == 0 ? 720u  : m_viewportHeight);

		// Search panel: left-centre.
		const float panelWidth  = std::clamp(vw * 0.55f, 480.0f, 720.0f);
		const float panelHeight = std::clamp(vh * 0.70f, 400.0f, 600.0f);
		const float panelX      = (vw - panelWidth) * 0.5f;
		const float panelY      = (vh - panelHeight) * 0.5f;

		m_searchState.panelBounds    = { panelX, panelY, panelWidth, panelHeight };
		m_searchState.layoutValid    = true;

		// My-listings panel: right-centre (offset slightly right).
		const float myWidth   = std::clamp(vw * 0.35f, 320.0f, 480.0f);
		const float myHeight  = panelHeight;
		const float myX       = panelX + panelWidth + 12.0f;
		const float myY       = panelY;

		m_myListingsState.panelBounds = { myX, myY, myWidth, myHeight };
		m_myListingsState.layoutValid = true;
	}

	void AuctionHouseUiPresenter::RebuildSearchPanel(const UIModel& model)
	{
		const UIAHState& ah = model.auctionHouse;
		m_searchState.visible     = ah.isOpen;
		m_searchState.totalCount  = ah.searchTotalCount;
		m_searchState.pageIndex   = ah.searchPageIndex;

		m_searchState.listings.clear();
		m_searchState.listings.reserve(ah.searchResults.size());
		for (const UIAHListingEntry& entry : ah.searchResults)
		{
			AHListingView view{};
			view.listingId    = entry.listingId;
			view.itemId       = entry.itemId;
			view.itemQuantity = entry.itemQuantity;
			view.startBid     = entry.startBid;
			view.buyout       = entry.buyout;
			view.currentBid   = entry.currentBid;
			view.expiresInSec = entry.expiresInSec;
			view.hasBid       = entry.hasBid;
			view.selected     = (entry.listingId == m_searchState.selectedListingId);
			view.displayName  = FindDisplayName(entry.itemId);
			view.iconPath     = FindIconPath(entry.itemId);

			// Affordability checks using the player's current gold balance.
			const uint64_t playerGold   = static_cast<uint64_t>(model.wallet.gold);
			const uint64_t minNextBid   = ComputeMinNextBid(entry.currentBid, entry.startBid);
			view.canAffordBid    = (playerGold >= minNextBid);
			view.canAffordBuyout = (entry.buyout != 0u) && (playerGold >= entry.buyout);

			m_searchState.listings.push_back(std::move(view));
		}
	}

	void AuctionHouseUiPresenter::RebuildMyListingsPanel(const UIModel& model)
	{
		const UIAHState& ah = model.auctionHouse;
		m_myListingsState.visible = ah.isOpen;

		m_myListingsState.listings.clear();
		m_myListingsState.listings.reserve(ah.myListings.size());
		for (const UIAHListingEntry& entry : ah.myListings)
		{
			AHListingView view{};
			view.listingId    = entry.listingId;
			view.itemId       = entry.itemId;
			view.itemQuantity = entry.itemQuantity;
			view.startBid     = entry.startBid;
			view.buyout       = entry.buyout;
			view.currentBid   = entry.currentBid;
			view.expiresInSec = entry.expiresInSec;
			view.hasBid       = entry.hasBid;
			view.selected     = (entry.listingId == m_myListingsState.selectedListingId);
			view.displayName  = FindDisplayName(entry.itemId);
			view.iconPath     = FindIconPath(entry.itemId);
			// Affordability fields not meaningful for own listings.
			view.canAffordBid    = false;
			view.canAffordBuyout = false;
			m_myListingsState.listings.push_back(std::move(view));
		}
	}

	void AuctionHouseUiPresenter::RebuildSearchDebugText()
	{
		m_searchState.debugText.clear();
		m_searchState.debugText += "[AHSearch]\n";
		m_searchState.debugText += "open=";
		m_searchState.debugText += m_searchState.visible ? "true" : "false";
		m_searchState.debugText += " page=";
		m_searchState.debugText += std::to_string(m_searchState.pageIndex);
		m_searchState.debugText += " total=";
		m_searchState.debugText += std::to_string(m_searchState.totalCount);
		m_searchState.debugText += " shown=";
		m_searchState.debugText += std::to_string(m_searchState.listings.size());
		m_searchState.debugText += "\n";
		for (const AHListingView& view : m_searchState.listings)
		{
			m_searchState.debugText += " id=";
			m_searchState.debugText += std::to_string(view.listingId);
			m_searchState.debugText += " item=";
			m_searchState.debugText += view.displayName.empty() ? std::to_string(view.itemId) : view.displayName;
			m_searchState.debugText += " qty=";
			m_searchState.debugText += std::to_string(view.itemQuantity);
			m_searchState.debugText += " bid=";
			m_searchState.debugText += std::to_string(view.currentBid);
			m_searchState.debugText += " buyout=";
			m_searchState.debugText += (view.buyout == 0u) ? "none" : std::to_string(view.buyout);
			m_searchState.debugText += " exp=";
			m_searchState.debugText += std::to_string(view.expiresInSec);
			m_searchState.debugText += "s\n";
		}
	}

	void AuctionHouseUiPresenter::RebuildMyListingsDebugText()
	{
		m_myListingsState.debugText.clear();
		m_myListingsState.debugText += "[AHMyListings]\n";
		m_myListingsState.debugText += "open=";
		m_myListingsState.debugText += m_myListingsState.visible ? "true" : "false";
		m_myListingsState.debugText += " count=";
		m_myListingsState.debugText += std::to_string(m_myListingsState.listings.size());
		m_myListingsState.debugText += "\n";
		for (const AHListingView& view : m_myListingsState.listings)
		{
			m_myListingsState.debugText += " id=";
			m_myListingsState.debugText += std::to_string(view.listingId);
			m_myListingsState.debugText += " item=";
			m_myListingsState.debugText += view.displayName.empty() ? std::to_string(view.itemId) : view.displayName;
			m_myListingsState.debugText += " qty=";
			m_myListingsState.debugText += std::to_string(view.itemQuantity);
			m_myListingsState.debugText += " currentBid=";
			m_myListingsState.debugText += std::to_string(view.currentBid);
			m_myListingsState.debugText += " hasBid=";
			m_myListingsState.debugText += view.hasBid ? "true" : "false";
			m_myListingsState.debugText += " exp=";
			m_myListingsState.debugText += std::to_string(view.expiresInSec);
			m_myListingsState.debugText += "s\n";
		}
	}

	std::string AuctionHouseUiPresenter::FindDisplayName(uint32_t itemId) const
	{
		for (const ItemMetaEntry& entry : m_metadata)
		{
			if (entry.itemId == itemId)
			{
				return entry.displayName;
			}
		}
		return "Item " + std::to_string(itemId);
	}

	std::string AuctionHouseUiPresenter::FindIconPath(uint32_t itemId) const
	{
		for (const ItemMetaEntry& entry : m_metadata)
		{
			if (entry.itemId == itemId)
			{
				return entry.iconPath;
			}
		}
		return {};
	}
}
