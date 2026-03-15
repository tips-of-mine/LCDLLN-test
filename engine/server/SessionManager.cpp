#include "engine/server/SessionManager.h"
#include "engine/core/Log.h"
#include <algorithm>
#include <chrono>
#include <random>

namespace engine::server
{
	namespace
	{
		constexpr uint64_t kInvalidSessionId = 0;
	}

	void SessionManager::SetConfig(const SessionManagerConfig& config)
	{
		m_config = config;
		LOG_DEBUG(Net, "[SessionManager] Config set: max_age_sec={} heartbeat_timeout_sec={} reconnection_window_sec={}",
			m_config.max_session_age_sec, m_config.heartbeat_timeout_sec, m_config.reconnection_window_sec);
	}

	SessionManagerConfig SessionManager::LoadConfig(const engine::core::Config& config)
	{
		SessionManagerConfig c;
		c.max_session_age_sec = config.GetInt("session.max_age_sec", 86400);
		c.heartbeat_timeout_sec = config.GetInt("session.heartbeat_timeout_sec", 120);
		c.reconnection_window_sec = config.GetInt("session.reconnection_window_sec", 300);
		std::string policy = config.GetString("session.duplicate_login_policy", "kick");
		if (policy == "refuse")
			c.duplicate_login_policy = DuplicateLoginPolicy::RefuseNew;
		else
			c.duplicate_login_policy = DuplicateLoginPolicy::KickExisting;
		return c;
	}

	bool SessionManager::isValid(const Session& s, Clock::time_point now) const
	{
		if (s.state != SessionState::Authenticated && s.state != SessionState::Active)
			return false;
		if (now > s.expires_at)
			return false;
		auto idle_sec = std::chrono::duration_cast<std::chrono::seconds>(now - s.last_seen).count();
		if (idle_sec > m_config.heartbeat_timeout_sec)
			return false;
		return true;
	}

	uint64_t SessionManager::generateSessionId()
	{
		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<uint64_t> dist(1, UINT64_MAX);
		return dist(gen);
	}

	uint64_t SessionManager::CreateSession(uint64_t account_id)
	{
		auto now = Clock::now();
		auto expires_at = now + std::chrono::seconds(m_config.max_session_age_sec);

		auto it = m_by_account_id.find(account_id);
		if (it != m_by_account_id.end())
		{
			uint64_t existing_id = it->second;
			auto sit = m_by_session_id.find(existing_id);
			if (sit != m_by_session_id.end() && isValid(sit->second, now))
			{
				if (m_config.duplicate_login_policy == DuplicateLoginPolicy::RefuseNew)
				{
					LOG_INFO(Net, "[SessionManager] CreateSession refused: account_id={} already has active session", account_id);
					return kInvalidSessionId;
				}
				Close(existing_id, SessionCloseReason::KickedByDuplicateLogin);
			}
		}

		uint64_t session_id = generateSessionId();
		if (session_id == kInvalidSessionId)
			return kInvalidSessionId;

		while (m_by_session_id.count(session_id))
		{
			session_id = generateSessionId();
			if (session_id == kInvalidSessionId)
				return kInvalidSessionId;
		}

		Session s;
		s.account_id = account_id;
		s.session_id = session_id;
		s.created_at = now;
		s.last_seen = now;
		s.expires_at = expires_at;
		s.state = SessionState::Created;

		m_by_session_id[session_id] = s;
		m_by_account_id[account_id] = session_id;

		LOG_INFO(Net, "[SessionManager] CreateSession OK: account_id={} session_id={}", account_id, session_id);
		return session_id;
	}

	bool SessionManager::Validate(uint64_t session_id) const
	{
		if (session_id == kInvalidSessionId)
			return false;
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return false;
		auto now = Clock::now();
		return isValid(it->second, now);
	}

	bool SessionManager::Touch(uint64_t session_id)
	{
		if (session_id == kInvalidSessionId)
			return false;
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return false;
		auto now = Clock::now();
		if (!isValid(it->second, now))
			return false;
		it->second.last_seen = now;
		LOG_DEBUG(Net, "[SessionManager] Touch OK: session_id={}", session_id);
		return true;
	}

	void SessionManager::Close(uint64_t session_id, SessionCloseReason reason)
	{
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return;
		uint64_t account_id = it->second.account_id;
		it->second.state = SessionState::Closed;
		m_by_account_id.erase(account_id);
		LOG_INFO(Net, "[SessionManager] Close: session_id={} account_id={} reason={}", session_id, account_id, SessionCloseReasonToString(reason));
	}

	void SessionManager::SetState(uint64_t session_id, SessionState state)
	{
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return;
		auto now = Clock::now();
		if (it->second.state != SessionState::Created && it->second.state != SessionState::Authenticated && it->second.state != SessionState::Active)
			return;
		if (now > it->second.expires_at)
			return;
		it->second.state = state;
		LOG_DEBUG(Net, "[SessionManager] SetState: session_id={} state={}", session_id, static_cast<int>(state));
	}

	std::optional<uint64_t> SessionManager::GetAccountId(uint64_t session_id) const
	{
		if (session_id == kInvalidSessionId)
			return std::nullopt;
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return std::nullopt;
		return it->second.account_id;
	}

	void SessionManager::EvictExpired()
	{
		auto now = Clock::now();
		for (auto it = m_by_session_id.begin(); it != m_by_session_id.end(); )
		{
			Session& s = it->second;
			if (s.state == SessionState::Closed || s.state == SessionState::Expired)
			{
				++it;
				continue;
			}
			bool past_max = now > s.expires_at;
			auto idle_sec = std::chrono::duration_cast<std::chrono::seconds>(now - s.last_seen).count();
			bool heartbeat_expired = idle_sec > m_config.heartbeat_timeout_sec;
			if (past_max || heartbeat_expired)
			{
				s.state = SessionState::Expired;
				m_by_account_id.erase(s.account_id);
				LOG_INFO(Net, "[SessionManager] EvictExpired: session_id={} (past_max={} heartbeat_expired={})", s.session_id, past_max, heartbeat_expired);
				++it;
			}
			else
				++it;
		}
	}

	std::string SessionCloseReasonToString(SessionCloseReason reason)
	{
		switch (reason)
		{
		case SessionCloseReason::Logout: return "Logout";
		case SessionCloseReason::KickedByDuplicateLogin: return "KickedByDuplicateLogin";
		case SessionCloseReason::HeartbeatTimeout: return "HeartbeatTimeout";
		case SessionCloseReason::MaxAgeExceeded: return "MaxAgeExceeded";
		case SessionCloseReason::Admin: return "Admin";
		case SessionCloseReason::Other: return "Other";
		default: return "Unknown";
		}
	}
}
