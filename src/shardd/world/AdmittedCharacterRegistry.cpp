#include "src/shardd/world/AdmittedCharacterRegistry.h"

namespace engine::server
{
	void AdmittedCharacterRegistry::Admit(uint64_t characterId, uint64_t accountId, uint64_t nowMs)
	{
		if (characterId == 0u)
		{
			return; // sentinelle « aucun personnage » : jamais admis.
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		m_admitted[characterId] = Entry{accountId, nowMs};
	}

	void AdmittedCharacterRegistry::Revoke(uint64_t characterId)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_admitted.erase(characterId);
	}

	bool AdmittedCharacterRegistry::IsAdmitted(uint64_t characterId, uint64_t nowMs) const
	{
		if (characterId == 0u)
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it == m_admitted.end())
		{
			return false;
		}
		return (nowMs - it->second.admittedAtMs) <= m_ttlMs;
	}

	uint64_t AdmittedCharacterRegistry::AdmittedAccountId(uint64_t characterId, uint64_t nowMs) const
	{
		if (characterId == 0u)
		{
			return 0u;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it == m_admitted.end() || (nowMs - it->second.admittedAtMs) > m_ttlMs)
		{
			return 0u;
		}
		return it->second.accountId;
	}

	size_t AdmittedCharacterRegistry::Count() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_admitted.size();
	}
}
