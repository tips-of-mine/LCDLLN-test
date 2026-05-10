#pragma once

#include "src/client/ui_common/UIModel.h"
#include "src/shared/core/Config.h"
#include "src/shared/network/TradePayloads.h"

#include <cstdint>
#include <functional>
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

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.27 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback. Mirror du pattern QuestUi/MailUi.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes Trade au master.
		/// Doit etre appele avant tout RequestBeginTrade/SetMyOffer/Lock/Commit/Cancel.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie TRADE_BEGIN_REQUEST vers \p targetAccountId (account_id direct).
		/// Reponse via OnTradeBeginResponse ; le partenaire recoit
		/// OnTradeBeginNotification.
		void RequestBeginTrade(uint64_t targetAccountId);

		/// Envoie TRADE_SET_OFFER_REQUEST avec l'offer courant cote sender
		/// (gold + items). Doit etre appele avec une session active
		/// (RequestBeginTrade reussi ou OnTradeBeginNotification recu).
		void SetMyOffer(uint64_t copperGold, const std::vector<uint64_t>& itemGuids);

		/// Envoie TRADE_LOCK_REQUEST. Le serveur valide et eventuellement passe
		/// en BothLocked si l'autre joueur a deja locke.
		void Lock();

		/// Envoie TRADE_COMMIT_REQUEST. N'est valide que si BothLocked cote
		/// FSM. Sinon le serveur retourne WrongState.
		void Commit();

		/// Envoie TRADE_CANCEL_REQUEST. Possible depuis n'importe quel etat
		/// non-terminal. Le serveur push CancelNotification aux 2 participants.
		void Cancel();

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit TRADE_BEGIN_RESPONSE. Si OK, met a jour la session active
		/// cote presenter (sessionId + partner) et ouvre l'UI.
		void OnTradeBeginResponse(const engine::network::TradeBeginResponsePayload& resp);

		/// Recoit TRADE_BEGIN_NOTIFICATION (push) : un autre joueur initie un
		/// trade vers nous. Met a jour la session active et ouvre l'UI.
		void OnTradeBeginNotification(const engine::network::TradeBeginNotificationPayload& note);

		/// Recoit TRADE_SET_OFFER_RESPONSE. ACK de notre propre SetMyOffer.
		void OnTradeSetOfferResponse(const engine::network::TradeSetOfferResponsePayload& resp);

		/// Recoit TRADE_LOCK_RESPONSE. ACK de notre Lock + nouveau state FSM.
		void OnTradeLockResponse(const engine::network::TradeLockResponsePayload& resp);

		/// Recoit TRADE_STATE_UPDATE_NOTIFICATION (push) : le partenaire a
		/// modifie son offer ou son lock. Met a jour l'offer miroir cote UI.
		void OnTradeStateUpdate(const engine::network::TradeStateUpdateNotificationPayload& note);

		/// Recoit TRADE_COMMIT_RESPONSE. ACK du commit final.
		void OnTradeCommitResponse(const engine::network::TradeCommitResponsePayload& resp);

		/// Recoit TRADE_CANCEL_NOTIFICATION (push) : la trade a ete annulee
		/// (par l'un des 2). Ferme l'UI cote presenter.
		void OnTradeCancelNotification(const engine::network::TradeCancelNotificationPayload& note);

		/// Accesseurs lecture seule pour tests/diagnostic.
		uint64_t GetCurrentSessionId() const { return m_currentSessionId; }
		uint64_t GetPartnerAccountId() const { return m_partnerAccountId; }
		uint8_t  GetCurrentState()    const { return m_fsmState; }

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

		// CMANGOS.27 (Phase 4.27 step 3+4) -- network state cote presenter.
		// La trade FSM cote master fait autorite ; on miroite simplement
		// l'etat ici pour que l'UI affiche la bonne phase.
		SendCallback          m_send;
		uint64_t              m_currentSessionId = 0;   ///< 0 si pas de trade active.
		uint64_t              m_partnerAccountId = 0;   ///< L'autre joueur (id account direct V1).
		uint8_t               m_fsmState         = 0;   ///< 0=Open, 1=LockedA, 2=LockedB, 3=BothLocked, 4=Committed, 5=Cancelled.
		uint64_t              m_partnerCopperGold = 0;  ///< Offer miroir cote partenaire (gold).
		std::vector<uint64_t> m_partnerItemGuids;       ///< Offer miroir cote partenaire (items).
	};
}
