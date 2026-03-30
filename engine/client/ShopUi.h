#pragma once

#include "engine/client/UIModel.h"
#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Pixel-space rectangle used by the shop panel layout.
	struct ShopRect
	{
		float x      = 0.0f;
		float y      = 0.0f;
		float width  = 0.0f;
		float height = 0.0f;
	};

	/// One vendor item entry ready for a shop panel row.
	struct ShopItemView
	{
		uint32_t    itemId     = 0;
		uint32_t    buyPrice   = 0;    ///< Gold cost to buy.
		uint32_t    sellPrice  = 0;    ///< Gold received when selling back.
		int32_t     stock      = -1;   ///< -1 means infinite.
		std::string displayName;
		std::string iconPath;
		bool        selected   = false;
		bool        canAfford  = false; ///< True when player gold >= buyPrice.
	};

	/// Fully resolved shop panel state for a rendering layer (M35.2).
	struct ShopPanelState
	{
		ShopRect               panelBounds{};
		std::string            vendorId;
		std::vector<ShopItemView> items;
		uint32_t               playerGold    = 0;
		uint32_t               selectedItemId = 0; ///< 0 when nothing is selected.
		std::string            debugText;
		bool                   visible       = false;
		bool                   layoutValid   = false;
	};

	/// Builds a vendor shop panel state from the shared UI model (M35.2).
	class ShopUiPresenter final
	{
	public:
		/// Construct an uninitialized shop presenter.
		ShopUiPresenter() = default;

		/// Release presenter resources.
		~ShopUiPresenter();

		/// Initialize the presenter and load item display metadata from content.
		bool Init(const engine::core::Config& config);

		/// Shutdown the presenter and release cached state.
		void Shutdown();

		/// Update the viewport-dependent layout for the shop panel.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot and rebuild the shop panel state.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Select one shop item by itemId for detail display (0 to deselect).
		bool SelectItem(uint32_t itemId);

		/// Return the immutable resolved shop panel state.
		const ShopPanelState& GetState() const { return m_state; }

	private:
		/// Load item display metadata from the inventory items metadata file.
		bool LoadMetadata(const engine::core::Config& config);

		/// Parse one metadata line using the `item|icon|name|description` format.
		bool ParseMetadataLine(std::string_view line, uint32_t& outItemId, std::string& outIcon, std::string& outName) const;

		/// Recompute panel rectangle after a viewport change.
		void RebuildLayout();

		/// Rebuild item rows from the current shop state.
		void RebuildItems(const UIModel& model);

		/// Rebuild textual debug dump of the shop panel state.
		void RebuildDebugText();

		/// Return the display name for one item id, or empty when metadata is absent.
		std::string FindDisplayName(uint32_t itemId) const;

		/// Return the icon path for one item id, or empty when metadata is absent.
		std::string FindIconPath(uint32_t itemId) const;

		struct ItemMetaEntry
		{
			uint32_t    itemId = 0;
			std::string iconPath;
			std::string displayName;
		};

		ShopPanelState           m_state{};
		std::vector<ItemMetaEntry> m_metadata;
		uint32_t                 m_viewportWidth  = 0;
		uint32_t                 m_viewportHeight = 0;
		std::string              m_metadataRelPath;
		bool                     m_initialized    = false;
	};
}
