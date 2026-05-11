// Wave 11 Persistence Cinematics — Implementation InMemoryCinematicStore.

#include "src/masterd/cinematics/InMemoryCinematicStore.h"

namespace engine::server::cinematics
{
	bool InMemoryCinematicStore::MarkSeen(uint64_t accountId, uint32_t sequenceId, uint64_t nowMs)
	{
		// nowMs n'est pas conserve en RAM : on n'expose pas de "first_seen_ts_ms"
		// dans cette impl ; le MysqlCinematicStore en fait usage. On accepte le
		// parametre par symetrie d'interface.
		(void)nowMs;
		std::lock_guard<std::mutex> lk(m_mutex);
		m_seen[accountId].insert(sequenceId);
		return true;
	}

	bool InMemoryCinematicStore::HasSeen(uint64_t accountId, uint32_t sequenceId) const
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_seen.find(accountId);
		if (it == m_seen.end()) return false;
		return it->second.count(sequenceId) > 0;
	}

	std::vector<uint32_t> InMemoryCinematicStore::ListSeen(uint64_t accountId) const
	{
		std::vector<uint32_t> out;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_seen.find(accountId);
		if (it == m_seen.end()) return out;
		out.reserve(it->second.size());
		for (uint32_t seq : it->second)
			out.push_back(seq);
		return out;
	}
}
