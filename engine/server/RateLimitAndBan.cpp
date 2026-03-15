#include "engine/server/RateLimitAndBan.h"
#include "engine/core/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace engine::server
{
	void RateLimitAndBan::SetConfig(const RateLimitAndBanConfig& config)
	{
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
		auto now = Clock::now();
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

	bool RateLimitAndBan::TryConsumeAuth(std::string_view ip)
	{
		std::string key(ip);
		if (IsBanned(ip))
			return false;
		double capacity = static_cast<double>(m_config.auth_per_minute);
		double refill_per_sec = capacity / 60.0;
		AuthState& state = m_by_ip[key];
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
		std::string key(ip);
		if (IsBanned(ip))
			return false;
		double capacity = static_cast<double>(m_config.register_per_hour);
		double refill_per_sec = capacity / 3600.0;
		AuthState& state = m_by_ip[key];
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
		std::string key(ip);
		AuthState& state = m_by_ip[key];
		state.failure_count++;
		if (state.failure_count >= m_config.max_failures_before_ban)
		{
			auto until = Clock::now() + std::chrono::seconds(m_config.ban_duration_sec);
			m_banned_until[key] = until;
			state.failure_count = 0;
			++m_counters.bans_issued;
			LOG_INFO(Net, "[RateLimitAndBan] IP banned: ip={} duration_sec={}", key, m_config.ban_duration_sec);
		}
	}

	bool RateLimitAndBan::IsBanned(std::string_view ip) const
	{
		auto it = m_banned_until.find(std::string(ip));
		if (it == m_banned_until.end())
			return false;
		if (Clock::now() < it->second)
			return true;
		return false;
	}

	void RateLimitAndBan::purgeOldEntries()
	{
		if (m_config.max_entries_per_map == 0)
			return;
		auto now = Clock::now();
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
		auto now = Clock::now();
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
		out = m_counters;
		out.bans_active = 0;
		auto now = Clock::now();
		for (const auto& p : m_banned_until)
		{
			if (now < p.second)
				++out.bans_active;
		}
	}
}
