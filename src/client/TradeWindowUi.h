#pragma once

#include "src/client/UIModel.h"
#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Pixel-space rectangle shared by trade window layout helpers.
	struct TradeRect
	{
		float x      = 0.0f;
		float y      = 0.0f;
		float width  = 0.0f;
		float height = 0.0f;
	};

	/// Resolved state for one item slot in the trade window (M35.3).
	struct TradeItemSlotState
	{
		TradeRect bounds{};
		uint32_t  slotIndex = 0;
		uint32_t  itemId    = 0;
		uint32_t  quantity  = 0;
		std::string label;
		bool      occupied  = false;
	};

	/// Resolved state for one side of the trade window (M35.3).
	struct TradeSidePanelState
	{
		TradeRect                      panelBounds{};
		std::string                    titleLabel;
		std::string                    goldLabel;
		std::vector<TradeItemSlotState> itemSlots;
		TradeRect                      lockButtonBounds{};
		TradeRect                      confirmButtonBounds{};
		std::string                    lockButtonLabel;
		std::string                    confirmButtonLabel;
		bool                           lockButtonEnabled    = false;
		bool                           confirmButtonEnabled = false;
		bool                           locked               = false;
		bool                           confirmed            = false;
	};

	/// Full resolved trade window layout for a rendering layer (M35.3).
	struct TradeWindowPanelState
	{
		TradeRect           windowBounds{};
		TradeSidePanelState selfPanel{};
		TradeSidePanelState otherPanel{};
		/// Human-readable status line (e.g. "Review — 4 s remaining").
		std::string         statusText;
		/// Informational text shown when the trade completed or was cancelled.
		std::string         resultText;
		bool                isVisible   = false;
		bool                layoutValid = false;
	};

	/// Builds a split trade window from the shared UI model (M35.3).
	///
	/// Layout:
	///   +—————————————————————————————+
	///   |   Self side  | Partner side |
	///   |  [slots 0-7] |  [slots 0-7] |
	///   |  Gold: XXX   |  Gold: XXX   |
	///   | [Lock] [Cnf] | [Lock] [Cnf] |
	///   +—————————————————————————————+
	class TradeWindowUiPresenter final
	{
	public:
		/// Construct an uninitialized trade window presenter.
		TradeWindowUiPresenter() = default;

		/// Release presenter resources.
		~TradeWindowUiPresenter();

		/// Initialize the presenter.
		bool Init(const engine::core::Config& config);

		/// Shutdown the presenter and release state.
		void Shutdown();

		/// Update the viewport-dependent layout.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Rebuild the panel from the current UI model trade window state.
		/// Call when \ref UIModelChangeTrade is set in the change mask.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Return the immutable resolved panel state.
		const TradeWindowPanelState& GetState() const { return m_state; }

	private:
		/// Recompute the full layout for a given viewport size.
		void RebuildLayout();

		/// Rebuild one side panel from a UI trade side.
		void RebuildSidePanel(
			TradeSidePanelState&  out,
			const UITradeSide&    side,
			bool                  isSelfSide,
			float                 panelX,
			float                 panelY,
			float                 panelW,
			float                 panelH,
			bool                  bothLocked,
			const UITradeWindowState& window);

		TradeWindowPanelState m_state{};
		uint32_t              m_viewportWidth  = 0;
		uint32_t              m_viewportHeight = 0;
		bool                  m_initialized    = false;
	};
}
