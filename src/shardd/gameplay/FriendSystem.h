#pragma once
// M32.1 — Server-side friend system: requests, bilateral relationship, online presence tracking.

#include "engine/server/ServerProtocol.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct MYSQL;

namespace engine::server
{
	/// Friendship record loaded from the DB (one directed row).
	struct FriendRecord
	{
		uint64_t         playerId = 0;
		uint64_t         friendId = 0;
		/// Friendship status matching DB column: 0=pending, 1=accepted, 2=declined.
		uint8_t          status   = 0;
		std::string      friendName;
	};

	/// Presence entry held in-memory for one connected player.
	struct FriendPresenceEntry
	{
		uint64_t       playerId = 0;
		std::string    playerName;
		PresenceStatus presence = PresenceStatus::Offline;
	};

	/// Server-side friend and presence manager (M32.1).
	///
	/// Handles /friend add, /friend accept, /friend decline, /friend remove,
	/// bilateral DB persistence, per-player rate limiting (≤5 requests/60 s),
	/// friend-list cap (200 per player) and in-memory online-status tracking.
	class FriendSystem final
	{
	public:
		FriendSystem() = default;

		/// Non-copyable, non-movable.
		FriendSystem(const FriendSystem&)            = delete;
		FriendSystem& operator=(const FriendSystem&) = delete;

		/// Initialize the system; \p mysql may be nullptr when DB is not configured (no-DB mode).
		/// Emits LOG_INFO on success or LOG_WARN when no DB is available.
		bool Init(MYSQL* mysql);

		/// Shut down the system and release in-memory state.
		/// Emits LOG_INFO on completion.
		void Shutdown();

		// ------------------------------------------------------------------
		// Friend request flow
		// ------------------------------------------------------------------

		/// Process /friend add <targetName> from \p requesterId.
		/// Validates the target exists, checks the friends cap and rate limit, inserts a
		/// pending row, and returns the target's account id (for routing the notify packet)
		/// on success or 0 on failure.
		uint64_t SendFriendRequest(uint64_t requesterId,
		                           std::string_view requesterName,
		                           std::string_view targetName,
		                           MYSQL*           mysql);

		/// Process /friend accept <requesterName> from \p accepterId.
		/// Updates both directed rows (requester→accepter and accepter→requester)
		/// to accepted=1 inside a transaction.
		/// Returns the requester's account id on success or 0 on failure.
		uint64_t AcceptFriendRequest(uint64_t         accepterId,
		                             std::string_view requesterName,
		                             MYSQL*           mysql);

		/// Process /friend decline <requesterName> from \p declinerId.
		/// Updates the pending row to declined=2.
		/// Returns the requester's account id on success or 0 on failure.
		uint64_t DeclineFriendRequest(uint64_t         declinerId,
		                              std::string_view requesterName,
		                              MYSQL*           mysql);

		/// Process /friend remove <friendName> from \p playerId.
		/// Deletes both directed rows inside a transaction.
		/// Returns true on success.
		bool RemoveFriend(uint64_t         playerId,
		                  std::string_view friendName,
		                  MYSQL*           mysql);

		// ------------------------------------------------------------------
		// Presence tracking
		// ------------------------------------------------------------------

		/// Mark a player as online (call on successful login).
		void SetPresence(uint64_t playerId, std::string_view playerName, PresenceStatus status);

		/// Mark a player as offline (call on disconnect/logout).
		void SetOffline(uint64_t playerId);

		/// Return the current presence for \p playerId (Offline when unknown).
		PresenceStatus GetPresence(uint64_t playerId) const;

		// ------------------------------------------------------------------
		// Queries
		// ------------------------------------------------------------------

		/// Load the accepted friends list for \p playerId from DB and decorate with in-memory presence.
		/// Returns an empty vector when DB is unavailable or no friends exist.
		std::vector<FriendRecord> GetFriendList(uint64_t playerId, MYSQL* mysql) const;

		/// Return the account ids of all currently-online accepted friends of \p playerId.
		/// Used to route presence-change notifications.
		std::vector<uint64_t> GetOnlineFriendIds(uint64_t playerId, MYSQL* mysql) const;

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Max accepted friends per player (M32.1 notes: 100-200 max).
		static constexpr size_t   kMaxFriendsPerPlayer       = 200;
		/// Rate limit window in seconds for friend requests.
		static constexpr int64_t  kRequestRateLimitWindowSec = 60;
		/// Max requests allowed per player within the window.
		static constexpr uint32_t kMaxRequestsPerWindow       = 5;

		using Clock = std::chrono::steady_clock;

		bool m_initialized = false;

		/// In-memory presence map: account_id → FriendPresenceEntry.
		std::unordered_map<uint64_t, FriendPresenceEntry> m_presence;

		/// Per-player request timestamps for rate limiting.
		std::unordered_map<uint64_t, std::deque<Clock::time_point>> m_requestRateLimit;

		/// Returns true when \p playerId has exceeded the friend-request rate limit.
		bool IsRateLimited(uint64_t playerId);

		/// Look up an account id by display name using a prepared statement.
		/// Returns 0 when not found or on error.
		uint64_t LookupPlayerIdByName(std::string_view name, MYSQL* mysql) const;

		/// Count accepted friends for \p playerId to enforce the cap.
		size_t CountAcceptedFriends(uint64_t playerId, MYSQL* mysql) const;
	};
}
