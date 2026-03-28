// M33.3 — User-based rate limiter implementation.
#include "engine/server/UserRateLimiter.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <chrono>

namespace engine::server
{
	void UserRateLimiter::SetConfig(const UserRateLimiterConfig& config)
	{
		m_config = config;
		LOG_INFO(Net, "[UserRateLimiter] Config set: chat_per_minute={} skills_per_sec={} max_entries={}",
			m_config.chat_per_minute, m_config.skills_per_sec, m_config.max_entries);
	}

	UserRateLimiterConfig UserRateLimiter::LoadConfig(const engine::core::Config& config)
	{
		UserRateLimiterConfig c;
		c.chat_per_minute  = static_cast<uint32_t>(config.GetInt("security.chat_per_minute",  10));
		c.skills_per_sec   = static_cast<double>(config.GetInt("security.skills_per_sec", 20));
		c.max_entries      = static_cast<size_t>(config.GetInt("security.user_rl_max_entries", 50000));
		return c;
	}

	bool UserRateLimiter::TryConsumeChat(uint64_t userId)
	{
		auto now = Clock::now();
		UserState& st = m_byUser[userId];
		st.last_active = now;

		// Sliding window: remove timestamps older than 60 seconds.
		const auto window_start = now - std::chrono::seconds(60);
		auto& dq = st.chat.timestamps;
		while (!dq.empty() && dq.front() < window_start)
			dq.pop_front();

		if (static_cast<uint32_t>(dq.size()) >= m_config.chat_per_minute)
		{
			++m_counters.chat_rate_limit_hits;
			LOG_DEBUG(Net, "[UserRateLimiter] Chat rate limited: userId={}", userId);
			return false;
		}
		dq.push_back(now);
		return true;
	}

	bool UserRateLimiter::TryConsumeSkill(uint64_t userId)
	{
		auto now = Clock::now();
		UserState& st = m_byUser[userId];
		st.last_active = now;

		SkillBucket& bucket = st.skills;
		if (bucket.last_refill == Clock::time_point{})
		{
			// First use: fill bucket.
			bucket.tokens     = m_config.skills_per_sec;
			bucket.last_refill = now;
		}
		else
		{
			const double elapsed =
				std::chrono::duration<double>(now - bucket.last_refill).count();
			bucket.tokens = std::min(m_config.skills_per_sec,
				bucket.tokens + elapsed * m_config.skills_per_sec);
			bucket.last_refill = now;
		}

		if (bucket.tokens < 1.0)
		{
			++m_counters.skill_rate_limit_hits;
			LOG_DEBUG(Net, "[UserRateLimiter] Skill rate limited: userId={}", userId);
			return false;
		}
		bucket.tokens -= 1.0;
		return true;
	}

	void UserRateLimiter::PurgeExpired()
	{
		const auto now    = Clock::now();
		const auto cutoff = now - std::chrono::minutes(5);

		for (auto it = m_byUser.begin(); it != m_byUser.end(); )
		{
			if (it->second.last_active < cutoff)
				it = m_byUser.erase(it);
			else
				++it;
		}

		// Enforce max_entries by evicting oldest-touched entries.
		if (m_config.max_entries > 0 && m_byUser.size() > m_config.max_entries)
		{
			while (m_byUser.size() > m_config.max_entries)
				m_byUser.erase(m_byUser.begin());
		}

		LOG_DEBUG(Net, "[UserRateLimiter] PurgeExpired done: tracked_users={}", m_byUser.size());
	}

	void UserRateLimiter::GetCounters(UserRateLimitCounters& out) const
	{
		out = m_counters;
	}
}
