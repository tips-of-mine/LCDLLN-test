#include "src/client/trade/TradeWindowUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

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

	// =========================================================================
	// CMANGOS.27 (Phase 4.27 step 3+4) -- Network actions (request senders)
	// =========================================================================

	void TradeWindowUiPresenter::RequestBeginTrade(uint64_t targetAccountId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[TradeWindowUi] RequestBeginTrade: no send callback");
			return;
		}
		if (m_currentSessionId != 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] RequestBeginTrade: already in trade (sid={})",
				m_currentSessionId);
			return;
		}
		const auto payload = engine::network::BuildTradeBeginRequestPayload(targetAccountId);
		if (!m_send(engine::network::kOpcodeTradeBeginRequest, payload))
		{
			LOG_WARN(Net, "[TradeWindowUi] RequestBeginTrade: send failed (target={})", targetAccountId);
			return;
		}
		LOG_INFO(Net, "[TradeWindowUi] TradeBeginRequest queued (target={})", targetAccountId);
	}

	void TradeWindowUiPresenter::SetMyOffer(uint64_t copperGold, const std::vector<uint64_t>& itemGuids)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[TradeWindowUi] SetMyOffer: no send callback");
			return;
		}
		if (m_currentSessionId == 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] SetMyOffer: no active trade session");
			return;
		}
		const auto payload = engine::network::BuildTradeSetOfferRequestPayload(
			m_currentSessionId, copperGold, itemGuids);
		if (!m_send(engine::network::kOpcodeTradeSetOfferRequest, payload))
		{
			LOG_WARN(Net, "[TradeWindowUi] SetMyOffer: send failed (sid={})", m_currentSessionId);
			return;
		}
		LOG_INFO(Net, "[TradeWindowUi] TradeSetOfferRequest queued (sid={} gold={} items={})",
			m_currentSessionId, copperGold, itemGuids.size());
	}

	void TradeWindowUiPresenter::Lock()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[TradeWindowUi] Lock: no send callback");
			return;
		}
		if (m_currentSessionId == 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] Lock: no active trade session");
			return;
		}
		const auto payload = engine::network::BuildTradeLockRequestPayload(m_currentSessionId);
		if (!m_send(engine::network::kOpcodeTradeLockRequest, payload))
		{
			LOG_WARN(Net, "[TradeWindowUi] Lock: send failed (sid={})", m_currentSessionId);
			return;
		}
		LOG_INFO(Net, "[TradeWindowUi] TradeLockRequest queued (sid={})", m_currentSessionId);
	}

	void TradeWindowUiPresenter::Commit()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[TradeWindowUi] Commit: no send callback");
			return;
		}
		if (m_currentSessionId == 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] Commit: no active trade session");
			return;
		}
		const auto payload = engine::network::BuildTradeCommitRequestPayload(m_currentSessionId);
		if (!m_send(engine::network::kOpcodeTradeCommitRequest, payload))
		{
			LOG_WARN(Net, "[TradeWindowUi] Commit: send failed (sid={})", m_currentSessionId);
			return;
		}
		LOG_INFO(Net, "[TradeWindowUi] TradeCommitRequest queued (sid={})", m_currentSessionId);
	}

	void TradeWindowUiPresenter::Cancel()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[TradeWindowUi] Cancel: no send callback");
			return;
		}
		if (m_currentSessionId == 0u)
		{
			// Pas d'erreur : on ne fait rien si pas de trade en cours.
			return;
		}
		const auto payload = engine::network::BuildTradeCancelRequestPayload(m_currentSessionId);
		if (!m_send(engine::network::kOpcodeTradeCancelRequest, payload))
		{
			LOG_WARN(Net, "[TradeWindowUi] Cancel: send failed (sid={})", m_currentSessionId);
			return;
		}
		LOG_INFO(Net, "[TradeWindowUi] TradeCancelRequest queued (sid={})", m_currentSessionId);
	}

	// =========================================================================
	// CMANGOS.27 (Phase 4.27 step 3+4) -- Master responses / push handlers
	// =========================================================================

	void TradeWindowUiPresenter::OnTradeBeginResponse(const engine::network::TradeBeginResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			using engine::network::TradeErrorCode;
			std::string err;
			switch (static_cast<TradeErrorCode>(resp.error))
			{
			case TradeErrorCode::PartnerOffline:  err = "Le partenaire n'est pas en ligne."; break;
			case TradeErrorCode::PartnerInTrade:  err = "Le partenaire est deja dans un echange."; break;
			case TradeErrorCode::SelfTrade:       err = "Impossible de commercer avec soi-meme."; break;
			case TradeErrorCode::Unauthorized:    err = "Session invalide. Reconnectez-vous."; break;
			default:                              err = "Erreur a l'ouverture de l'echange."; break;
			}
			m_state.resultText = "Trade refused: " + err;
			LOG_WARN(Net, "[TradeWindowUi] OnTradeBeginResponse error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		// OK : on note la session active. L'UI reelle (ouverture du panneau)
		// est gouvernee par le UIModel.tradeWindow.isOpen ; ici on enregistre
		// juste le contexte reseau qui pilotera SetMyOffer/Lock/Commit/Cancel.
		m_currentSessionId = resp.sessionId;
		m_partnerAccountId = resp.partnerAccountId;
		m_fsmState         = 0u; // Open
		m_partnerCopperGold = 0;
		m_partnerItemGuids.clear();
		m_state.resultText.clear();
		LOG_INFO(Net, "[TradeWindowUi] OnTradeBeginResponse OK sid={} partner={}",
			m_currentSessionId, m_partnerAccountId);
	}

	void TradeWindowUiPresenter::OnTradeBeginNotification(const engine::network::TradeBeginNotificationPayload& note)
	{
		// Quelqu'un nous a initie un trade. En V1 : auto-accept (UI s'ouvre,
		// le joueur peut Cancel s'il ne veut pas). Si on a deja une session,
		// on log et on ignore (le serveur a normalement deja rejete).
		if (m_currentSessionId != 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] OnTradeBeginNotification ignored: already in trade sid={}",
				m_currentSessionId);
			return;
		}
		m_currentSessionId = note.sessionId;
		m_partnerAccountId = note.partnerAccountId;
		m_fsmState         = 0u;
		m_partnerCopperGold = 0;
		m_partnerItemGuids.clear();
		m_state.resultText.clear();
		LOG_INFO(Net, "[TradeWindowUi] OnTradeBeginNotification incoming sid={} from={}",
			m_currentSessionId, m_partnerAccountId);
	}

	void TradeWindowUiPresenter::OnTradeSetOfferResponse(const engine::network::TradeSetOfferResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] OnTradeSetOfferResponse error code={}",
				static_cast<unsigned>(resp.error));
			m_state.resultText = "Offer rejected by server.";
			return;
		}
		LOG_DEBUG(Net, "[TradeWindowUi] OnTradeSetOfferResponse OK");
	}

	void TradeWindowUiPresenter::OnTradeLockResponse(const engine::network::TradeLockResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] OnTradeLockResponse error code={}",
				static_cast<unsigned>(resp.error));
			m_state.resultText = "Lock refused by server.";
			return;
		}
		m_fsmState = resp.newState;
		LOG_INFO(Net, "[TradeWindowUi] OnTradeLockResponse newState={}", static_cast<unsigned>(resp.newState));
	}

	void TradeWindowUiPresenter::OnTradeStateUpdate(const engine::network::TradeStateUpdateNotificationPayload& note)
	{
		if (note.sessionId != m_currentSessionId)
		{
			// Push pour une autre session (race au reset, etc.) : on ignore.
			LOG_DEBUG(Net, "[TradeWindowUi] OnTradeStateUpdate sid mismatch (got={} expected={})",
				note.sessionId, m_currentSessionId);
			return;
		}
		m_fsmState          = note.state;
		m_partnerCopperGold = note.partnerCopperGold;
		m_partnerItemGuids  = note.partnerItemGuids;
		LOG_INFO(Net, "[TradeWindowUi] OnTradeStateUpdate sid={} state={} partnerGold={} partnerItems={}",
			note.sessionId, static_cast<unsigned>(note.state),
			note.partnerCopperGold, note.partnerItemGuids.size());

		// Etat terminal Committed : on libere le contexte cote client (le
		// serveur a deja End()-e la session). L'UIModel propage isDone qui
		// affichera resultText "Trade completed".
		if (m_fsmState == 4u)
		{
			LOG_INFO(Net, "[TradeWindowUi] StateUpdate -> Committed, resetting session sid={}",
				m_currentSessionId);
			m_currentSessionId = 0;
			m_partnerAccountId = 0;
			m_partnerCopperGold = 0;
			m_partnerItemGuids.clear();
		}
	}

	void TradeWindowUiPresenter::OnTradeCommitResponse(const engine::network::TradeCommitResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			LOG_WARN(Net, "[TradeWindowUi] OnTradeCommitResponse error code={}",
				static_cast<unsigned>(resp.error));
			m_state.resultText = "Commit refused by server.";
			return;
		}
		// Le state passera a Committed via le push StateUpdate (parallele a
		// la response). Ici on log juste le succes ack par le master.
		LOG_INFO(Net, "[TradeWindowUi] OnTradeCommitResponse OK");
	}

	void TradeWindowUiPresenter::OnTradeCancelNotification(const engine::network::TradeCancelNotificationPayload& note)
	{
		if (note.sessionId != m_currentSessionId && m_currentSessionId != 0u)
		{
			LOG_DEBUG(Net, "[TradeWindowUi] OnTradeCancelNotification sid mismatch (got={} expected={})",
				note.sessionId, m_currentSessionId);
			return;
		}
		m_state.resultText = note.reason.empty()
			? std::string("Trade cancelled.")
			: ("Trade cancelled: " + note.reason);
		m_state.isVisible = false;
		LOG_INFO(Net, "[TradeWindowUi] OnTradeCancelNotification sid={} reason='{}'",
			note.sessionId, note.reason);
		// Reset le contexte : la session est terminale cote serveur.
		m_currentSessionId = 0;
		m_partnerAccountId = 0;
		m_fsmState = 5u; // Cancelled
		m_partnerCopperGold = 0;
		m_partnerItemGuids.clear();
	}
}
