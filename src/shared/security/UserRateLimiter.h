#pragma once

// M33.3 — User-based rate limiter for in-game actions (chat 10 msg/min, skills 20/sec).
// Token bucket for skills; sliding window for chat.
// Thread-safe only if caller serialises access (single-worker v1).

#include "engine/core/Config.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace engine::server
{
	/// Counters for user-based rate limiting (chat + skills).
	struct UserRateLimitCounters
	{
		uint64_t chat_rate_limit_hits  = 0;
		uint64_t skill_rate_limit_hits = 0;
	};

	/// Configuration for user-based rate limiting.
	struct UserRateLimiterConfig
	{
		/// Max chat messages per minute per user (sliding window). Default: 10.
		uint32_t chat_per_minute = 10;
		/// Max skill uses per second per user (token bucket capacity). Default: 20.
		double skills_per_sec = 20.0;
		/// Max tracked user entries. Oldest entries purged when exceeded (0 = no limit).
		size_t max_entries = 50000;
	};

	/// User-based rate limiter: chat (10/min sliding window) + skills (20/sec token bucket).
	/// Apply per authenticated userId before processing costly game actions.
	class UserRateLimiter
	{
	public:
		UserRateLimiter() = default;

		/// Set configuration. Call before use.
		void SetConfig(const UserRateLimiterConfig& config);

		/// Build config from engine Config
		/// (keys: security.chat_per_minute, security.skills_per_sec, security.user_rl_max_entries).
		static UserRateLimiterConfig LoadConfig(const engine::core::Config& config);

		/// Returns true if chat message is allowed for this user. False = rate limited.
		/// Records the attempt and advances the sliding window.
		bool TryConsumeChat(uint64_t userId);

		/// Returns true if skill use is allowed for this user. False = rate limited.
		/// Consumes one token from the per-user bucket.
		bool TryConsumeSkill(uint64_t userId);

		/// Remove stale entries to limit memory growth. Call periodically (e.g., every minute).
		void PurgeExpired();

		/// Fill \a out with current counters for observability.
		void GetCounters(UserRateLimitCounters& out) const;

	private:
		using Clock = std::chrono::steady_clock;

		/// Sliding window of message timestamps within the last minute.
		struct ChatWindow
		{
			std::deque<Clock::time_point> timestamps;
		};

		/// Token bucket for skills.
		struct SkillBucket
		{
			double tokens    = 0.0;
			Clock::time_point last_refill{};
		};

		struct UserState
		{
			ChatWindow  chat;
			SkillBucket skills;
			Clock::time_point last_active{};
		};

		UserRateLimiterConfig m_config;
		mutable UserRateLimitCounters m_counters;
		std::unordered_map<uint64_t, UserState> m_byUser;
	};
}
