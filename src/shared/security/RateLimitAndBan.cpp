#include "src/shared/security/RateLimitAndBan.h"
#include "src/shared/core/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace engine::server
{
	void RateLimitAndBan::SetClock(engine::core::IClock* c)
	{
		m_clock = c;
	}

	engine::core::IClock& RateLimitAndBan::clock() const
	{
		return m_clock ? *m_clock : engine::core::SteadyClock::Instance();
	}

	void RateLimitAndBan::SetConfig(const RateLimitAndBanConfig& config)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = config;
		LOG_DEBUG(Net, "[RateLimitAndBan] Config set: auth_per_min={} register_per_hour={} max_failures={} ban_duration_sec={}",
			m_config.auth_per_minute, m_config.register_per_hour, m_config.max_failures_before_ban, m_config.ban_duration_sec);
	}

	RateLimitAndBanConfig RateLimitAndBan::LoadConfig(const engine::core::Config& config)
	{
		RateLimitAndBanConfig c;
		c.auth_per_minute = config.GetInt("security.auth_per_minute", 10);
		c.register_per_hour = config.GetInt("security.register_per_hour", 3);
		c.max_failures_before_ban = config.GetInt("security.max_failures_before_ban", 5);
		c.ban_duration_sec = config.GetInt("security.ban_duration_sec", 3600);
		c.max_entries_per_map = static_cast<size_t>(config.GetInt("security.max_entries_per_map", 10000));
		return c;
	}

	bool RateLimitAndBan::tryConsume(TokenBucket& bucket, double capacity, double refill_per_sec)
	{
		auto now = clock().Now();
		double elapsed = std::chrono::duration<double>(now - bucket.last_refill).count();
		bucket.tokens = std::min(capacity, bucket.tokens + elapsed * refill_per_sec);
		bucket.last_refill = now;
		if (bucket.tokens >= 1.0)
		{
			bucket.tokens -= 1.0;
			return true;
		}
		return false;
	}

	RateLimitAndBan::AuthState& RateLimitAndBan::getOrCreateState(const std::string& key)
	{
		auto [it, inserted] = m_by_ip.try_emplace(key);
		if (inserted)
		{
			// Warm-start : à la 1ère insertion, le bucket démarre PLEIN au cap
			// configuré et last_refill = now. Sans ce warm-start, le bucket
			// démarrait tokens=0 + last_refill=epoch, ce qui rendait le 1er
			// consume dépendant de l'uptime du process (sur un master fraîchement
			// (re)démarré, register_per_hour=3 → refill ≈ 0.000833 tok/s →
			// le 1er register d'une nouvelle IP était refusé pendant ~1h après
			// le boot). Bug masqué en CI car les tests étaient exclus du `-E`
			// ctest, révélé par la réintégration FU-2.
			auto now = clock().Now();
			it->second.auth_bucket.tokens = static_cast<double>(m_config.auth_per_minute);
			it->second.auth_bucket.last_refill = now;
			it->second.register_bucket.tokens = static_cast<double>(m_config.register_per_hour);
			it->second.register_bucket.last_refill = now;
		}
		return it->second;
	}

	bool RateLimitAndBan::TryConsumeAuth(std::string_view ip)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key(ip);
		if (isBannedUnlocked(ip))
			return false;
		double capacity = static_cast<double>(m_config.auth_per_minute);
		double refill_per_sec = capacity / 60.0;
		AuthState& state = getOrCreateState(key);
		if (!tryConsume(state.auth_bucket, capacity, refill_per_sec))
		{
			++m_counters.rate_limit_hits_auth;
			LOG_DEBUG(Net, "[RateLimitAndBan] Auth rate limited: ip={}", key);
			return false;
		}
		return true;
	}

	bool RateLimitAndBan::TryConsumeRegister(std::string_view ip)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key(ip);
		if (isBannedUnlocked(ip))
			return false;
		double capacity = static_cast<double>(m_config.register_per_hour);
		double refill_per_sec = capacity / 3600.0;
		AuthState& state = getOrCreateState(key);
		if (!tryConsume(state.register_bucket, capacity, refill_per_sec))
		{
			++m_counters.rate_limit_hits_register;
			LOG_DEBUG(Net, "[RateLimitAndBan] Register rate limited: ip={}", key);
			return false;
		}
		return true;
	}

	void RateLimitAndBan::RecordAuthFailure(std::string_view ip)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key(ip);
		AuthState& state = getOrCreateState(key);
		state.failure_count++;
		if (state.failure_count >= m_config.max_failures_before_ban)
		{
			auto until = clock().Now() + std::chrono::seconds(m_config.ban_duration_sec);
			m_banned_until[key] = until;
			state.failure_count = 0;
			++m_counters.bans_issued;
			LOG_INFO(Net, "[RateLimitAndBan] IP banned: ip={} duration_sec={}", key, m_config.ban_duration_sec);
		}
	}

	bool RateLimitAndBan::IsBanned(std::string_view ip) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return isBannedUnlocked(ip);
	}

	bool RateLimitAndBan::isBannedUnlocked(std::string_view ip) const
	{
		auto it = m_banned_until.find(std::string(ip));
		if (it == m_banned_until.end())
			return false;
		if (clock().Now() < it->second)
			return true;
		return false;
	}

	void RateLimitAndBan::purgeOldEntries()
	{
		if (m_config.max_entries_per_map == 0)
			return;
		auto now = clock().Now();
		for (auto it = m_banned_until.begin(); it != m_banned_until.end(); )
		{
			if (now >= it->second)
				it = m_banned_until.erase(it);
			else
				++it;
		}
		while (m_by_ip.size() > m_config.max_entries_per_map)
		{
			m_by_ip.erase(m_by_ip.begin());
		}
		while (m_banned_until.size() > m_config.max_entries_per_map)
		{
			m_banned_until.erase(m_banned_until.begin());
		}
	}

	void RateLimitAndBan::PurgeExpired()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto now = clock().Now();
		for (auto it = m_banned_until.begin(); it != m_banned_until.end(); )
		{
			if (now >= it->second)
				it = m_banned_until.erase(it);
			else
				++it;
		}
		purgeOldEntries();
	}

	void RateLimitAndBan::GetCounters(SecurityCounters& out) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		out = m_counters;
		out.bans_active = 0;
		auto now = clock().Now();
		for (const auto& p : m_banned_until)
		{
			if (now < p.second)
				++out.bans_active;
		}
	}
}
