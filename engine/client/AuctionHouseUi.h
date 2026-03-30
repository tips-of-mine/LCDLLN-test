#pragma once

/// M35.4 — Auction house UI presenter: search panel, post-listing panel, my-listings.
///
/// Consumes the shared UIModel (via UIModelChangeAH notifications) and exposes
/// fully-resolved AH panel states ready for a rendering layer.
/// Follows the same presenter pattern as ShopUiPresenter (M35.2).

#include "engine/client/UIModel.h"
#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Pixel-space rectangle reused by the AH panel layouts.
	struct AHRect
	{
		float x      = 0.0f;
		float y      = 0.0f;
		float width  = 0.0f;
		float height = 0.0f;
	};

	/// One resolved listing row shown in the search or my-listings panel (M35.4).
	struct AHListingView
	{
		uint64_t    listingId    = 0;
		uint32_t    itemId       = 0;
		uint32_t    itemQuantity = 1;
		uint64_t    startBid     = 0;
		uint64_t    buyout       = 0;   ///< 0 = no buyout price.
		uint64_t    currentBid   = 0;
		uint32_t    expiresInSec = 0;
		bool        hasBid       = false;
		bool        selected     = false;
		bool        canAffordBid = false;    ///< True when player gold >= next valid bid.
		bool        canAffordBuyout = false; ///< True when player gold >= buyout price.
		std::string displayName;             ///< Human-readable item name (from metadata).
		std::string iconPath;                ///< Content-relative icon path.
	};

	/// Fully resolved AH search panel state (M35.4).
	struct AHSearchPanelState
	{
		AHRect                    panelBounds{};
		std::vector<AHListingView> listings;
		uint32_t                  totalCount    = 0;
		uint32_t                  pageIndex     = 0;
		uint64_t                  selectedListingId = 0; ///< 0 when nothing selected.
		std::string               debugText;
		bool                      visible       = false;
		bool                      layoutValid   = false;
	};

	/// Fully resolved "my listings" panel state (M35.4).
	struct AHMyListingsPanelState
	{
		AHRect                    panelBounds{};
		std::vector<AHListingView> listings;
		uint64_t                  selectedListingId = 0;
		std::string               debugText;
		bool                      visible       = false;
		bool                      layoutValid   = false;
	};

	/// Last AH action outcome shown as a status message (M35.4).
	struct AHActionStatus
	{
		bool        success       = false;
		uint64_t    listingId     = 0;
		std::string errorReason;
		bool        hasResult     = false; ///< True when a result is pending display.
	};

	/// Builds AH panel states from the shared UI model (M35.4).
	class AuctionHouseUiPresenter final
	{
	public:
		/// Construct an uninitialised presenter.
		AuctionHouseUiPresenter() = default;

		/// Release presenter resources.
		~AuctionHouseUiPresenter();

		/// Initialise the presenter and load item display metadata from content.
		bool Init(const engine::core::Config& config);

		/// Release cached state and emit shutdown log.
		void Shutdown();

		/// Update the viewport-dependent layout for the AH panels.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot and rebuild both panel states.
		/// Should be called when \c changeMask includes UIModelChangeAH.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Select one listing by id in the search panel (0 to deselect).
		bool SelectSearchListing(uint64_t listingId);

		/// Select one listing by id in the my-listings panel (0 to deselect).
		bool SelectMyListing(uint64_t listingId);

		/// Return the immutable search panel state.
		const AHSearchPanelState& GetSearchState() const { return m_searchState; }

		/// Return the immutable my-listings panel state.
		const AHMyListingsPanelState& GetMyListingsState() const { return m_myListingsState; }

		/// Return the last action status (bid result, post result, …).
		const AHActionStatus& GetActionStatus() const { return m_actionStatus; }

	private:
		/// Load item display metadata from the inventory items metadata file.
		bool LoadMetadata(const engine::core::Config& config);

		/// Recompute panel rectangles after a viewport change.
		void RebuildLayout();

		/// Rebuild search panel listing rows from the AH UI model state.
		void RebuildSearchPanel(const UIModel& model);

		/// Rebuild my-listings panel rows from the AH UI model state.
		void RebuildMyListingsPanel(const UIModel& model);

		/// Build a debug text dump of the search panel.
		void RebuildSearchDebugText();

		/// Build a debug text dump of the my-listings panel.
		void RebuildMyListingsDebugText();

		/// Return the display name for one item id, or empty when metadata is absent.
		std::string FindDisplayName(uint32_t itemId) const;

		/// Return the content-relative icon path for one item id, or empty when absent.
		std::string FindIconPath(uint32_t itemId) const;

		struct ItemMetaEntry
		{
			uint32_t    itemId = 0;
			std::string iconPath;
			std::string displayName;
		};

		AHSearchPanelState    m_searchState{};
		AHMyListingsPanelState m_myListingsState{};
		AHActionStatus         m_actionStatus{};
		std::vector<ItemMetaEntry> m_metadata;
		uint32_t               m_viewportWidth  = 0;
		uint32_t               m_viewportHeight = 0;
		std::string            m_metadataRelPath;
		bool                   m_initialized    = false;
	};
}
