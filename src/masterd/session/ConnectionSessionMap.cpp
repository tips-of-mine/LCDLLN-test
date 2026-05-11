#include "src/masterd/session/ConnectionSessionMap.h"

namespace engine::server
{
	void ConnectionSessionMap::Add(uint32_t connId, uint64_t sessionId)
	{
		std::lock_guard lock(m_mutex);
		m_connToSession[connId] = sessionId;
	}

	void ConnectionSessionMap::Remove(uint32_t connId)
	{
		std::lock_guard lock(m_mutex);
		m_connToSession.erase(connId);
	}

	std::optional<uint64_t> ConnectionSessionMap::GetSessionId(uint32_t connId) const
	{
		std::lock_guard lock(m_mutex);
		auto it = m_connToSession.find(connId);
		if (it == m_connToSession.end())
			return std::nullopt;
		return it->second;
	}

	std::optional<uint32_t> ConnectionSessionMap::FindConnIdForSession(uint64_t sessionId) const
	{
		std::lock_guard lock(m_mutex);
		for (const auto& [conn, sess] : m_connToSession)
		{
			if (sess == sessionId)
				return conn;
		}
		return std::nullopt;
	}

	std::vector<std::pair<uint32_t, uint64_t>> ConnectionSessionMap::Snapshot() const
	{
		std::vector<std::pair<uint32_t, uint64_t>> out;
		std::lock_guard lock(m_mutex);
		out.reserve(m_connToSession.size());
		for (const auto& [conn, sess] : m_connToSession)
		{
			out.emplace_back(conn, sess);
		}
		return out;
	}

	std::vector<std::pair<uint32_t, uint64_t>> ConnectionSessionMap::CollectExpired(const SessionManager& sessionManager)
	{
		std::vector<std::pair<uint32_t, uint64_t>> out;
		std::lock_guard lock(m_mutex);
		for (auto it = m_connToSession.begin(); it != m_connToSession.end(); )
		{
			if (!sessionManager.Validate(it->second))
			{
				out.emplace_back(it->first, it->second);
				it = m_connToSession.erase(it);
			}
			else
				++it;
		}
		return out;
	}
}
