#include "src/masterd/shards/ShardPlayerPresenceCache.h"

namespace engine::server
{
	void ShardPlayerPresenceCache::Update(uint32_t shardId, const std::vector<engine::network::ShardPlayerPresence>& players)
	{
		std::lock_guard lock(m_mutex);
		// Retire d'abord toutes les entrées actuelles de ce shard (un joueur déconnecté
		// disparaît du rapport et doit donc sortir du cache).
		for (auto it = m_byAccount.begin(); it != m_byAccount.end();)
		{
			if (it->second.shardId == shardId)
				it = m_byAccount.erase(it);
			else
				++it;
		}
		// Réinsère l'ensemble courant rapporté par le shard.
		for (const auto& p : players)
		{
			if (p.accountId == 0)
				continue;
			Entry e;
			e.accountId = p.accountId;
			e.characterId = p.characterId;
			e.level = p.level;
			e.zoneId = p.zoneId;
			e.shardId = shardId;
			m_byAccount[p.accountId] = e;
		}
	}

	void ShardPlayerPresenceCache::Clear(uint32_t shardId)
	{
		std::lock_guard lock(m_mutex);
		for (auto it = m_byAccount.begin(); it != m_byAccount.end();)
		{
			if (it->second.shardId == shardId)
				it = m_byAccount.erase(it);
			else
				++it;
		}
	}

	std::optional<ShardPlayerPresenceCache::Entry> ShardPlayerPresenceCache::Get(uint64_t accountId) const
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byAccount.find(accountId);
		if (it == m_byAccount.end())
			return std::nullopt;
		return it->second;
	}

	std::vector<ShardPlayerPresenceCache::Entry> ShardPlayerPresenceCache::Snapshot() const
	{
		std::lock_guard lock(m_mutex);
		std::vector<Entry> out;
		out.reserve(m_byAccount.size());
		for (const auto& [accountId, e] : m_byAccount)
		{
			(void)accountId;
			out.push_back(e);
		}
		return out;
	}
}
