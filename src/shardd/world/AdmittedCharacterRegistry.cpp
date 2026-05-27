#include "src/shardd/world/AdmittedCharacterRegistry.h"

namespace engine::server
{
	void AdmittedCharacterRegistry::Admit(uint64_t characterId, uint64_t accountId,
		std::string_view characterName, std::string_view gender, uint64_t nowMs)
	{
		if (characterId == 0u)
		{
			return; // sentinelle « aucun personnage » : jamais admis.
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		// Préserve le nom/genre déjà connus si la nouvelle admission est anonyme (rafraîchissement
		// d'horodatage via un ticket sans nom après un EnterWorld qui les avait fournis).
		Entry& entry = m_admitted[characterId];
		entry.accountId = accountId;
		entry.admittedAtMs = nowMs;
		if (!characterName.empty())
		{
			entry.characterName.assign(characterName);
		}
		if (!gender.empty())
		{
			entry.gender.assign(gender);
		}
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

	std::string AdmittedCharacterRegistry::AdmittedCharacterName(uint64_t characterId, uint64_t nowMs) const
	{
		if (characterId == 0u)
		{
			return {};
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it == m_admitted.end() || (nowMs - it->second.admittedAtMs) > m_ttlMs)
		{
			return {};
		}
		return it->second.characterName;
	}

	std::string AdmittedCharacterRegistry::AdmittedGender(uint64_t characterId, uint64_t nowMs) const
	{
		if (characterId == 0u)
		{
			return {};
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it == m_admitted.end() || (nowMs - it->second.admittedAtMs) > m_ttlMs)
		{
			return {};
		}
		return it->second.gender;
	}

	size_t AdmittedCharacterRegistry::Count() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_admitted.size();
	}
}
