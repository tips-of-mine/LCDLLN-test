#pragma once

#include "src/shared/network/ReplicationTypes.h"
#include "src/shared/network/ServerProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	struct ConnectedClient;

	/// Current phase of a trade session (M35.3).
	enum class TradePhase : uint8_t
	{
		/// Both players are adding items/gold.
		Negotiation = 0,
		/// Both sides have locked; 5 s anti-scam review window is running.
		Review = 1,
		/// One or both sides have confirmed after the review window.
		Confirming = 2,
	};

	/// Runtime state for one offer side inside an active trade session (M35.3).
	struct TradeSide
	{
		uint32_t             clientId   = 0;
		uint32_t             goldAmount = 0;
		bool                 locked     = false;
		bool                 confirmed  = false;
		std::vector<ItemStack> items;
	};

	/// One active trade session between two players (M35.3).
	struct TradeSession
	{
		uint32_t   sideAClientId = 0; ///< Initiator (player who typed /trade).
		uint32_t   sideBClientId = 0; ///< Target (player who accepted).
		TradeSide  sideA{};
		TradeSide  sideB{};
		TradePhase phase = TradePhase::Negotiation;
		/// Server tick when the review phase started (used to count 5 s).
		uint32_t   reviewStartTick = 0;
	};

	/// Pending incoming trade request (before acceptance) (M35.3).
	struct PendingTradeRequest
	{
		uint32_t    initiatorClientId = 0;
		uint32_t    targetClientId    = 0;
		std::string initiatorName;
	};

	/// Result returned by trade operation helpers (M35.3).
	enum class TradeOpResult : uint8_t
	{
		Ok           = 0,
		NotInTrade   = 1,
		WrongPhase   = 2,
		SlotsFull    = 3,
		InsufficientItems = 4,
		InsufficientGold  = 5,
		RangeExceeded     = 6,
		AlreadyInTrade    = 7,
		PendingExists     = 8,
		InvalidTarget     = 9,
	};

	/// Maximum range (metres) between two players for a trade to be valid (M35.3).
	inline constexpr float kTradeMaxRangeMeters = 10.0f;

	/// Duration of the 5 s anti-scam review window expressed in server ticks (M35.3).
	/// Computed at runtime from tickHz; stored as tick count in TradeSession.
	inline constexpr uint32_t kTradeReviewSeconds = 5u;

	/// Server-side trade session manager: request flow, item management, validation
	/// and atomic item/gold swap between two ConnectedClient instances (M35.3).
	class TradeSystem final
	{
	public:
		TradeSystem() = default;

		/// Initialize the trade system.
		/// @param tickHz  Server fixed tick rate — used to compute review tick count.
		void Init(uint16_t tickHz);

		/// Shutdown the trade system and log remaining open sessions.
		void Shutdown();

		// ------------------------------------------------------------------
		// Request / accept / decline flow
		// ------------------------------------------------------------------

		/// Record a pending trade request from \p initiator to the player named \p targetName.
		/// Returns TradeOpResult::Ok when the request is queued.
		TradeOpResult SendTradeRequest(
			const ConnectedClient& initiator,
			const ConnectedClient& target,
			std::string& outError);

		/// Accept the pending request and create an active session.
		/// Returns TradeOpResult::Ok when the session is open.
		TradeOpResult AcceptTradeRequest(
			const ConnectedClient& acceptor,
			std::string& outError);

		/// Decline the pending request and remove it.
		TradeOpResult DeclineTradeRequest(
			const ConnectedClient& decliner,
			std::string& outError);

		// ------------------------------------------------------------------
		// Active session operations
		// ------------------------------------------------------------------

		/// Add one item stack to the caller's offer side.
		TradeOpResult AddItem(
			const ConnectedClient& caller,
			uint32_t itemId,
			uint32_t quantity,
			std::string& outError);

		/// Replace the gold offer for the caller's side.
		TradeOpResult SetGold(
			const ConnectedClient& caller,
			uint32_t goldAmount,
			std::string& outError);

		/// Lock the caller's side (enter review phase when both sides are locked).
		/// Sets reviewStartTick when the session enters Review phase.
		TradeOpResult Lock(
			const ConnectedClient& caller,
			uint32_t currentTick,
			std::string& outError);

		/// Confirm the caller's side (irreversible; requires Review phase and both locked).
		/// Validates items + gold then swaps inventories when both sides confirm.
		TradeOpResult Confirm(
			ConnectedClient& caller,
			ConnectedClient& partner,
			std::string& outError);

		/// Cancel the session (either side or server-forced).
		TradeOpResult Cancel(
			uint32_t clientId,
			std::string& outError);

		// ------------------------------------------------------------------
		// State queries
		// ------------------------------------------------------------------

		/// Return the active session for \p clientId, or nullptr.
		TradeSession*       FindSession(uint32_t clientId);
		const TradeSession* FindSession(uint32_t clientId) const;

		/// Return the pending request addressed to \p targetClientId, or nullptr.
		PendingTradeRequest* FindPendingRequest(uint32_t targetClientId);

		/// Return the partner clientId for the player currently in a session.
		/// Returns 0 when \p clientId is not in any session.
		uint32_t GetPartnerClientId(uint32_t clientId) const;

		/// Build the TradeWindowUpdateMessage seen by \p viewerClientId.
		TradeWindowUpdateMessage BuildWindowUpdate(uint32_t viewerClientId, uint32_t currentTick) const;

		/// Tick — advance review phase timer and unlock stale sessions.
		void Tick(uint32_t currentTick);

		/// Number of currently open sessions (for diagnostics).
		size_t ActiveSessionCount() const { return m_sessions.size(); }

	private:
		/// Euclidean 2D distance between two clients (XZ plane).
		static float Distance2D(const ConnectedClient& a, const ConnectedClient& b);

		/// Validate that both sides have the offered items in their inventories.
		bool ValidateItems(
			const ConnectedClient& clientA,
			const ConnectedClient& clientB,
			const TradeSession&    session,
			std::string&           outError) const;

		/// Move gold and items between the two clients (server-authoritative).
		void ApplySwap(ConnectedClient& clientA, ConnectedClient& clientB, TradeSession& session);

		/// Remove a session and log its closure.
		void RemoveSession(uint32_t sessionIndex, std::string_view reason);

		/// Return the mutable TradeSide that belongs to \p clientId, or nullptr.
		TradeSide* SelectSide(TradeSession& session, uint32_t clientId);
		const TradeSide* SelectSide(const TradeSession& session, uint32_t clientId) const;

		/// Find the index of the session that contains \p clientId, or SIZE_MAX.
		size_t FindSessionIndex(uint32_t clientId) const;

		std::vector<TradeSession>       m_sessions;
		std::vector<PendingTradeRequest> m_pendingRequests;
		uint16_t                        m_tickHz          = 20;
		uint32_t                        m_reviewTickCount = 0;
	};
}
