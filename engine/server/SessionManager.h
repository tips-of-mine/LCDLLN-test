#pragma once

#include "engine/core/Config.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::server
{
	/// Session state machine: created → authenticated → active → expired/closed.
	enum class SessionState : uint8_t
	{
		Created,
		Authenticated,
		Active,
		Expired,
		Closed
	};

	/// Reason for closing a session (logging / observability).
	enum class SessionCloseReason : uint8_t
	{
		Logout,
		KickedByDuplicateLogin,
		HeartbeatTimeout,
		MaxAgeExceeded,
		Admin,
		Other
	};

	/// Policy when duplicate login is attempted: refuse new or kick existing session (policy v1).
	enum class DuplicateLoginPolicy : uint8_t
	{
		RefuseNew,   ///< New login refused if account already has an active session.
		KickExisting ///< Existing session closed, new session created.
	};

	/// Configuration for SessionManager (align with server config; heartbeat/timeout in seconds).
	struct SessionManagerConfig
	{
		/// Max session lifetime in seconds (e.g. 24h = 86400). Enforced regardless of Touch.
		int64_t max_session_age_sec = 86400;
		/// Seconds without Touch before session is considered expired (heartbeat timeout).
		int64_t heartbeat_timeout_sec = 300;
		/// Reconnection window: within this many seconds after last_seen, session can be resumed (same as heartbeat for v1).
		int64_t reconnection_window_sec = 300;
		/// How to handle duplicate login for the same account.
		DuplicateLoginPolicy duplicate_login_policy = DuplicateLoginPolicy::KickExisting;
	};

	/// Single session record: account_id, session_id, timestamps, state.
	struct Session
	{
		uint64_t account_id = 0;
		uint64_t session_id = 0;
		std::chrono::steady_clock::time_point created_at{};
		std::chrono::steady_clock::time_point last_seen{};
		std::chrono::steady_clock::time_point expires_at{};
		SessionState state = SessionState::Created;
	};

	/// Global session manager: one active session per account, 64-bit session_id, 24h expiry, reconnection window.
	/// Lookup by session_id and by account_id. Uses monotonic clock for timeouts. Thread-safe if external mutex is used (v1 single-threaded server).
	class SessionManager
	{
	public:
		SessionManager() = default;

		/// Set configuration (e.g. from server config / config.json). Call before CreateSession.
		void SetConfig(const SessionManagerConfig& config);

		/// Build SessionManagerConfig from engine config (keys: session.max_age_sec, session.heartbeat_timeout_sec, session.reconnection_window_sec, session.duplicate_login_policy "refuse"|"kick").
		static SessionManagerConfig LoadConfig(const engine::core::Config& config);

		/// Create a session for \a account_id. Returns session_id (64-bit) or 0 if refused (duplicate and policy RefuseNew).
		/// If policy is KickExisting, any existing session for this account is closed first.
		uint64_t CreateSession(uint64_t account_id);

		/// Returns true if \a session_id exists, is in Authenticated/Active, not past expires_at, and within heartbeat/reconnection window.
		bool Validate(uint64_t session_id) const;

		/// Updates last_seen for \a session_id. Returns true if session exists and is valid; false otherwise.
		bool Touch(uint64_t session_id);

		/// Closes the session and sets state to Closed. \a reason is for logging.
		void Close(uint64_t session_id, SessionCloseReason reason);

		/// Optional: advance session to Authenticated or Active (e.g. after auth success). No-op if invalid.
		void SetState(uint64_t session_id, SessionState state);

		/// Evict expired sessions (state set to Expired, then can be removed from lookups). Call periodically.
		void EvictExpired();

	private:
		using Clock = std::chrono::steady_clock;

		SessionManagerConfig m_config;
		std::unordered_map<uint64_t, Session> m_by_session_id;
		std::unordered_map<uint64_t, uint64_t> m_by_account_id;

		bool isValid(const Session& s, Clock::time_point now) const;
		uint64_t generateSessionId();
	};

	/// Returns a human-readable string for SessionCloseReason (logging).
	std::string SessionCloseReasonToString(SessionCloseReason reason);
}
