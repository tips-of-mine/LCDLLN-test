#pragma once

#include "engine/core/Config.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server
{
	/// Counters exportables for observability (pre-M23). Thread-safe only if caller serialises access (v1).
	struct SecurityCounters
	{
		uint64_t rate_limit_hits_auth = 0;
		uint64_t rate_limit_hits_register = 0;
		uint64_t bans_issued = 0;
		uint64_t bans_active = 0;
	};

	/// Configuration for rate limiting and IP ban (align with server config).
	struct RateLimitAndBanConfig
	{
		/// Max AUTH attempts per minute per IP (token bucket capacity).
		int64_t auth_per_minute = 10;
		/// Max REGISTER attempts per hour per IP (token bucket capacity).
		int64_t register_per_hour = 3;
		/// Number of auth failures after which IP is banned.
		int64_t max_failures_before_ban = 5;
		/// Ban duration in seconds.
		int64_t ban_duration_sec = 3600;
		/// Max entries to keep per map (purge oldest when exceeded; 0 = no limit).
		size_t max_entries_per_map = 10000;
	};

	/// Token bucket + failure count + IP ban. Apply before costly auth/DB. In-memory + periodic purge.
	class RateLimitAndBan
	{
	public:
		RateLimitAndBan() = default;

		/// Set configuration. Call before use.
		void SetConfig(const RateLimitAndBanConfig& config);

		/// Build config from engine Config (keys: security.auth_per_minute, security.register_per_hour, security.max_failures_before_ban, security.ban_duration_sec).
		static RateLimitAndBanConfig LoadConfig(const engine::core::Config& config);

		/// Returns true if allowed (consumes one token). False if rate limited or banned. Check IsBanned first if you want to reject banned IPs before any work.
		bool TryConsumeAuth(std::string_view ip);

		/// Returns true if allowed (consumes one token). False if rate limited or banned.
		bool TryConsumeRegister(std::string_view ip);

		/// Records an auth failure for \a ip. May trigger ban when count reaches max_failures_before_ban.
		void RecordAuthFailure(std::string_view ip);

		/// Returns true if \a ip is currently banned (call before costly processing).
		bool IsBanned(std::string_view ip) const;

		/// Remove expired bans and old bucket entries to limit memory. Call periodically.
		void PurgeExpired();

		/// Fill \a out with current counters (exportables pre-M23).
		void GetCounters(SecurityCounters& out) const;

	private:
		using Clock = std::chrono::steady_clock;

		struct TokenBucket
		{
			double tokens = 0.0;
			Clock::time_point last_refill{};
		};
		struct AuthState
		{
			TokenBucket auth_bucket;
			TokenBucket register_bucket;
			int failure_count = 0;
		};

		RateLimitAndBanConfig m_config;
		mutable SecurityCounters m_counters;
		std::unordered_map<std::string, AuthState> m_by_ip;
		std::unordered_map<std::string, Clock::time_point> m_banned_until;

		bool tryConsume(TokenBucket& bucket, double capacity, double refill_per_sec);
		void purgeOldEntries();
	};
}
