#include "src/client/trade/TradeWindowUi.h"

#include "src/shared/core/Log.h"

#include <algorithm>

namespace engine::client
{
	TradeWindowUiPresenter::~TradeWindowUiPresenter()
	{
		if (m_initialized)
		{
			Shutdown();
		}
	}

	bool TradeWindowUiPresenter::Init(const engine::core::Config& /*config*/)
	{
		if (m_initialized)
		{
			LOG_WARN(Gameplay, "[TradeWindowUi] Init called on already-initialized presenter");
			return true;
		}
		m_state       = {};
		m_initialized = true;
		LOG_INFO(Gameplay, "[TradeWindowUi] Init OK");
		return true;
	}

	void TradeWindowUiPresenter::Shutdown()
	{
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Gameplay, "[TradeWindowUi] Destroyed");
	}

	bool TradeWindowUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[TradeWindowUi] SetViewportSize: not initialized");
			return false;
		}
		m_viewportWidth  = width;
		m_viewportHeight = height;
		RebuildLayout();
		return true;
	}

	bool TradeWindowUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[TradeWindowUi] ApplyModel: not initialized");
			return false;
		}
		if ((changeMask & UIModelChangeTrade) == 0)
		{
			return true;
		}

		const UITradeWindowState& window = model.tradeWindow;

		if (!window.isOpen && !window.isDone && window.cancelReason.empty())
		{
			m_state.isVisible  = false;
			m_state.resultText.clear();
			return true;
		}

		m_state.isVisible = window.isOpen;

		if (window.isDone)
		{
			m_state.isVisible  = false;
			m_state.resultText = "Trade completed successfully.";
			LOG_INFO(Gameplay, "[TradeWindowUi] Trade completed — window closed");
			return true;
		}

		if (!window.cancelReason.empty())
		{
			m_state.isVisible  = false;
			m_state.resultText = "Trade cancelled: " + window.cancelReason;
			LOG_INFO(Gameplay, "[TradeWindowUi] Trade cancelled: {}", window.cancelReason);
			return true;
		}

		m_state.resultText.clear();

		/// Rebuild layout to ensure bounds are up to date.
		if (!m_state.layoutValid || m_viewportWidth == 0)
		{
			RebuildLayout();
		}

		const bool bothLocked = window.selfSide.locked && window.otherSide.locked;

		/// Status text (review phase or waiting for lock).
		if (window.reviewTicksRemaining > 0)
		{
			/// Round up to whole seconds for display.
			const uint32_t secondsRemaining = (window.reviewTicksRemaining + 19u) / 20u;
			m_state.statusText = "Review phase — " + std::to_string(secondsRemaining) + " s remaining";
		}
		else if (bothLocked)
		{
			m_state.statusText = "Both sides locked — confirm when ready";
		}
		else if (window.selfSide.locked)
		{
			m_state.statusText = "Waiting for partner to lock…";
		}
		else
		{
			m_state.statusText = "Add items and gold, then press Lock";
		}

		const float panelW = m_state.windowBounds.width * 0.5f;
		const float panelH = m_state.windowBounds.height;

		RebuildSidePanel(
			m_state.selfPanel,
			window.selfSide,
			true,
			m_state.windowBounds.x,
			m_state.windowBounds.y,
			panelW,
			panelH,
			bothLocked,
			window);

		RebuildSidePanel(
			m_state.otherPanel,
			window.otherSide,
			false,
			m_state.windowBounds.x + panelW,
			m_state.windowBounds.y,
			panelW,
			panelH,
			bothLocked,
			window);

		LOG_DEBUG(Gameplay,
		          "[TradeWindowUi] ApplyModel: open={} selfLocked={} otherLocked={} status='{}'",
		          m_state.isVisible,
		          window.selfSide.locked,
		          window.otherSide.locked,
		          m_state.statusText);
		return true;
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	void TradeWindowUiPresenter::RebuildLayout()
	{
		if (m_viewportWidth == 0 || m_viewportHeight == 0)
		{
			m_state.layoutValid = false;
			return;
		}

		/// Trade window occupies 60% width and 50% height, centred on screen.
		const float windowW = static_cast<float>(m_viewportWidth)  * 0.60f;
		const float windowH = static_cast<float>(m_viewportHeight) * 0.50f;
		m_state.windowBounds = {
			(static_cast<float>(m_viewportWidth)  - windowW) * 0.5f,
			(static_cast<float>(m_viewportHeight) - windowH) * 0.5f,
			windowW,
			windowH
		};

		m_state.layoutValid = true;
		LOG_DEBUG(Gameplay,
		          "[TradeWindowUi] Layout: x={} y={} w={} h={}",
		          m_state.windowBounds.x, m_state.windowBounds.y,
		          m_state.windowBounds.width, m_state.windowBounds.height);
	}

	void TradeWindowUiPresenter::RebuildSidePanel(
		TradeSidePanelState&       out,
		const UITradeSide&         side,
		bool                       isSelfSide,
		float                      panelX,
		float                      panelY,
		float                      panelW,
		float                      panelH,
		bool                       bothLocked,
		const UITradeWindowState&  window)
	{
		constexpr float kPad      = 8.0f;
		constexpr float kSlotSize = 40.0f;
		constexpr float kCols     = 4.0f;
		constexpr uint8_t kMaxSlots = 8;

		out.panelBounds   = { panelX, panelY, panelW, panelH };
		out.titleLabel    = isSelfSide ? "Your offer" : "Partner's offer";
		out.goldLabel     = "Gold: " + std::to_string(side.goldAmount);
		out.locked        = side.locked;
		out.confirmed     = side.confirmed;

		/// Item slots (2 rows of 4).
		out.itemSlots.clear();
		out.itemSlots.reserve(kMaxSlots);
		for (uint8_t i = 0; i < kMaxSlots; ++i)
		{
			TradeItemSlotState slot{};
			const float col = static_cast<float>(i % static_cast<int>(kCols));
			const float row = static_cast<float>(i / static_cast<int>(kCols));
			slot.bounds    = {
				panelX + kPad + col * (kSlotSize + kPad),
				panelY + 28.0f + row * (kSlotSize + kPad),
				kSlotSize,
				kSlotSize
			};
			slot.slotIndex = i;
			if (i < side.items.size())
			{
				slot.itemId   = side.items[i].itemId;
				slot.quantity = side.items[i].quantity;
				slot.label    = std::to_string(slot.itemId) + "x" + std::to_string(slot.quantity);
				slot.occupied = true;
			}
			out.itemSlots.push_back(std::move(slot));
		}

		/// Lock button: enabled only on the self side when in Negotiation phase.
		const float btnY    = panelY + panelH - 36.0f;
		const float btnW    = (panelW - kPad * 3.0f) * 0.5f;
		out.lockButtonBounds    = { panelX + kPad,            btnY, btnW, 28.0f };
		out.confirmButtonBounds = { panelX + kPad * 2 + btnW, btnY, btnW, 28.0f };

		if (isSelfSide)
		{
			out.lockButtonLabel       = side.locked    ? "Locked ✓"   : "Lock";
			out.confirmButtonLabel    = side.confirmed ? "Confirmed ✓" : "Confirm";
			out.lockButtonEnabled    = !side.locked;
			/// Confirm button becomes available after both sides locked and the
			/// 5 s review window has elapsed (reviewTicksRemaining == 0).
			out.confirmButtonEnabled =
				bothLocked &&
				window.reviewTicksRemaining == 0 &&
				!side.confirmed;
		}
		else
		{
			out.lockButtonLabel       = side.locked    ? "Locked ✓"   : "Waiting…";
			out.confirmButtonLabel    = side.confirmed ? "Confirmed ✓" : "Waiting…";
			out.lockButtonEnabled    = false;
			out.confirmButtonEnabled = false;
		}
	}
}
