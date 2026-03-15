#include "engine/server/ConnectionSessionMap.h"

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
