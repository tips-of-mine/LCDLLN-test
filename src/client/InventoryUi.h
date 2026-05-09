#pragma once

#include "engine/client/UIModel.h"
#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Pixel-space rectangle used by the inventory presenter layout.
	struct InventoryRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	/// Item metadata loaded from content data for inventory rendering and tooltips.
	struct InventoryItemMetadata
	{
		uint32_t itemId = 0;
		std::string iconPath;
		std::string displayName;
		std::string description;
	};

	/// One resolved inventory slot ready for a UI layer to render.
	struct InventorySlotState
	{
		InventoryRect bounds{};
		uint32_t slotIndex = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
		std::string iconPath;
		std::string label;
		bool occupied = false;
		bool hovered = false;
	};

	/// Tooltip state built from the currently hovered inventory slot.
	struct InventoryTooltipState
	{
		InventoryRect bounds{};
		std::string title;
		std::string description;
		std::string iconPath;
		bool visible = false;
	};

	/// One transient pickup feedback line derived from inventory deltas.
	struct InventoryPickupFeedback
	{
		std::string text;
		float remainingSeconds = 0.0f;
		bool active = false;
	};

	/// Fully resolved inventory panel state for a rendering layer.
	struct InventoryPanelState
	{
		InventoryRect panelBounds{};
		InventoryRect gridBounds{};
		uint32_t columns = 0;
		uint32_t rows = 0;
		std::vector<InventorySlotState> slots;
		InventoryTooltipState tooltip{};
		std::vector<InventoryPickupFeedback> pickupFeedback;
		std::string debugText;
		bool layoutValid = false;
	};

	/// Builds an inventory grid, tooltip and pickup feedback from the shared UI model.
	class InventoryUiPresenter final
	{
	public:
		/// Construct an uninitialized inventory presenter.
		InventoryUiPresenter() = default;

		/// Release presenter resources.
		~InventoryUiPresenter();

		/// Initialize the presenter and load item metadata from a content-relative file.
		bool Init(const engine::core::Config& config);

		/// Shutdown the presenter and release cached state.
		void Shutdown();

		/// Update the viewport-dependent layout for the inventory panel.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot and rebuild visible inventory slots.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Update hover-dependent tooltip state from a client-space mouse position.
		bool UpdateHover(float mouseX, float mouseY);

		/// Advance transient pickup feedback timers.
		bool Tick(float deltaSeconds);

		/// Return the immutable resolved inventory panel state.
		const InventoryPanelState& GetState() const { return m_state; }

		/// M35.2 — begin dragging one occupied slot (e.g. for vendor sell-back drop).
		bool TryBeginDrag(float mouseX, float mouseY);

		/// Drop drag state without sending a transaction.
		void CancelDrag();

		bool IsDragging() const { return m_dragActive; }

		/// Valid when \ref IsDragging.
		bool GetDragSource(uint32_t& outSlotIndex, uint32_t& outItemId, uint32_t& outQty) const;

	private:
		/// Load item metadata from the configured content-relative path.
		bool LoadMetadata(const engine::core::Config& config);

		/// Parse one metadata file line using the `item|icon|name|description` format.
		bool ParseMetadataLine(std::string_view line, InventoryItemMetadata& outMetadata) const;

		/// Recompute slot rectangles after a viewport change.
		void RebuildLayout();

		/// Rebuild slot contents from the shared UI inventory model.
		void RebuildSlots(const UIModel& model);

		/// Refresh pickup feedback by comparing the current and previous inventory states.
		void RefreshPickupFeedback(const UIModel& model);

		/// Refresh tooltip state after slots or hover change.
		void RefreshTooltip();

		/// Return metadata for one item id, or null when the data file has no entry.
		const InventoryItemMetadata* FindMetadata(uint32_t itemId) const;

		/// Rebuild a textual dump of the resolved inventory panel state.
		void RebuildDebugText();

		void SyncDragWithModel(const UIModel& model);

		InventoryPanelState m_state{};
		std::vector<InventoryItemMetadata> m_metadata;
		std::vector<engine::server::ItemStack> m_previousInventory;
		uint32_t m_hoveredSlotIndex = UINT32_MAX;
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		std::string m_relativeMetadataPath;
		bool m_initialized = false;
		bool m_dragActive = false;
		uint32_t m_dragSlotIndex = 0;
		uint32_t m_dragItemId = 0;
		uint32_t m_dragQuantity = 0;
	};
}
