#include "src/shardd/world/ShardPresenceService.h"

namespace engine::server
{
	void ShardPresenceService::SetOnline(uint64_t accountId, uint64_t characterId, std::string characterName,
		uint32_t level, uint32_t zoneId, PresenceStatus status)
	{
		if (accountId == 0)
			return;
		std::lock_guard lock(m_mutex);
		Entry& e = m_byAccount[accountId];
		e.accountId = accountId;
		e.characterId = characterId;
		e.characterName = std::move(characterName);
		e.level = level;
		e.zoneId = zoneId;
		e.status = status;
	}

	void ShardPresenceService::SetOffline(uint64_t accountId)
	{
		std::lock_guard lock(m_mutex);
		m_byAccount.erase(accountId);
	}

	void ShardPresenceService::UpdateZone(uint64_t accountId, uint32_t zoneId)
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byAccount.find(accountId);
		if (it != m_byAccount.end())
			it->second.zoneId = zoneId;
	}

	void ShardPresenceService::UpdateLevel(uint64_t accountId, uint32_t level)
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byAccount.find(accountId);
		if (it != m_byAccount.end())
			it->second.level = level;
	}

	bool ShardPresenceService::IsOnline(uint64_t accountId) const
	{
		std::lock_guard lock(m_mutex);
		return m_byAccount.find(accountId) != m_byAccount.end();
	}

	PresenceStatus ShardPresenceService::GetStatus(uint64_t accountId) const
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byAccount.find(accountId);
		return it != m_byAccount.end() ? it->second.status : PresenceStatus::Offline;
	}

	std::optional<ShardPresenceService::Entry> ShardPresenceService::Get(uint64_t accountId) const
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byAccount.find(accountId);
		if (it == m_byAccount.end())
			return std::nullopt;
		return it->second;
	}

	std::vector<uint64_t> ShardPresenceService::OnlineAccountIdsAmong(const std::vector<uint64_t>& candidates) const
	{
		std::lock_guard lock(m_mutex);
		std::vector<uint64_t> out;
		out.reserve(candidates.size());
		for (uint64_t id : candidates)
		{
			if (m_byAccount.find(id) != m_byAccount.end())
				out.push_back(id);
		}
		return out;
	}

	std::vector<ShardPresenceService::Entry> ShardPresenceService::Snapshot() const
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
