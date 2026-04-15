#pragma once

#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	/// Maximum number of distinct item stacks a player may offer in one trade (M35.3).
	inline constexpr uint8_t kMaxTradeItemSlots = 12;

	/// Range in metres beyond which a trade request is rejected (M35.3).
	inline constexpr float kTradeMaxRangeMeters = 10.0f;

	/// Ticks that must elapse after both players lock before Confirm becomes valid (M35.3).
	/// At 20 Hz tick rate this is 5 seconds.
	inline constexpr uint32_t kTradeLockReviewTicks = 100;

	/// Trade session phases (M35.3).
	enum class TradePhase : uint8_t
	{
		/// Request sent by A, waiting for B to accept/decline.
		Pending = 0,
		/// Both players are in the trade window, can add/remove items and gold.
		Active = 1,
		/// Both players have pressed Lock; 5-second review timer running.
		Locked = 2,
		/// Both players have confirmed after the review timer; server is committing.
		Confirmed = 3,
		/// Trade is over (success or cancelled).
		Closed = 4
	};

	/// One player's slot in an active trade session (M35.3).
	struct TradeSlot
	{
		uint32_t clientId = 0;
		uint32_t characterKey = 0;
		std::string displayName;
		std::vector<ItemStack> offeredItems;
		uint32_t offeredGold = 0;
		bool locked = false;
		bool confirmed = false;
	};

	/// Authoritative trade session between two players (M35.3).
	struct TradeSession
	{
		uint32_t sessionId = 0;
		TradeSlot slotA{};
		TradeSlot slotB{};
		TradePhase phase = TradePhase::Pending;
		/// Tick at which both players locked; used to enforce the 5-second review timer.
		uint32_t bothLockedAtTick = 0;

		/// Return the slot that owns \p clientId, or nullptr.
		TradeSlot* FindSlot(uint32_t clientId);
		const TradeSlot* FindSlot(uint32_t clientId) const;

		/// Return the opponent slot of \p clientId, or nullptr.
		TradeSlot* FindOpponentSlot(uint32_t clientId);
		const TradeSlot* FindOpponentSlot(uint32_t clientId) const;
	};

	/// Server-side trade manager: pending requests + active sessions (M35.3).
	class PlayerTradeManager final
	{
	public:
		PlayerTradeManager() = default;

		/// Initialise the manager.  Emits LOG_INFO on success.
		void Init();

		/// Release all pending/active sessions.  Emits LOG_INFO on shutdown.
		void Shutdown();

		/// Return true when Init() has been called and Shutdown() has not.
		bool IsInitialized() const { return m_initialized; }

		// ------------------------------------------------------------------
		// Pending request lifecycle
		// ------------------------------------------------------------------

		/// Record a trade request from \p initiatorClientId targeting \p targetClientId.
		/// Returns false when \p initiatorClientId already has a pending request.
		bool AddPendingRequest(uint32_t initiatorClientId, uint32_t targetClientId);

		/// Return the initiator clientId for a pending request targeting \p targetClientId,
		/// or 0 when no such request exists.
		uint32_t FindPendingRequestInitiator(uint32_t targetClientId) const;

		/// Remove any pending request initiated by or targeting \p clientId.
		void CancelPendingRequest(uint32_t clientId);

		// ------------------------------------------------------------------
		// Active session lifecycle
		// ------------------------------------------------------------------

		/// Create an active trade session between two players and return its sessionId.
		uint32_t CreateSession(
			uint32_t clientIdA, uint32_t charKeyA, std::string_view displayNameA,
			uint32_t clientIdB, uint32_t charKeyB, std::string_view displayNameB);

		/// Return the active session for \p clientId, or nullptr.
		TradeSession* FindSession(uint32_t clientId);
		const TradeSession* FindSession(uint32_t clientId) const;

		/// Remove a session by sessionId (called on completion or cancel).
		void RemoveSession(uint32_t sessionId);

		/// Expire any pending requests or sessions that involve disconnected clientIds.
		void CancelSessionsForClient(uint32_t clientId);

	private:
		struct PendingRequest
		{
			uint32_t initiatorClientId = 0;
			uint32_t targetClientId = 0;
		};

		std::vector<PendingRequest> m_pendingRequests;
		std::vector<TradeSession> m_sessions;
		uint32_t m_nextSessionId = 1;
		bool m_initialized = false;
	};
}
