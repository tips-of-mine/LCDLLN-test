#include "src/shardd/world/AdmittedCharacterRegistry.h"

namespace engine::server
{
	void AdmittedCharacterRegistry::Admit(uint64_t characterId, uint64_t accountId,
		std::string_view characterName, std::string_view gender, uint64_t guildId, uint64_t nowMs,
		const std::array<uint8_t, 32>* udpSessionKey)
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
		// Roadmap-7 — même règle de préservation : une admission sans guilde
		// (ticket TCP, master legacy) ne doit pas écraser la guilde poussée par
		// un EnterWorld antérieur.
		if (guildId != 0u)
		{
			entry.guildId = guildId;
		}
		// ETAPE3.1 — même règle : une admission sans clé (ticket TCP, master legacy)
		// garde la clé poussée par l'EnterWorld.
		if (udpSessionKey != nullptr)
		{
			if (!entry.hasUdpSessionKey || entry.udpSessionKey != *udpSessionKey)
				entry.usedUdpSalts.clear(); // nouvelle clé : nouveaux sels
			entry.udpSessionKey = *udpSessionKey;
			entry.hasUdpSessionKey = true;
		}
	}

	void AdmittedCharacterRegistry::SetOwnerGeneration(uint64_t characterId, uint64_t generation)
	{
		if (characterId == 0u || generation == 0u)
		{
			return;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it != m_admitted.end())
		{
			it->second.ownerGeneration = generation;
		}
	}

	uint64_t AdmittedCharacterRegistry::AdmittedOwnerGeneration(uint64_t characterId, uint64_t nowMs) const
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
		return it->second.ownerGeneration;
	}

	void AdmittedCharacterRegistry::SetUdpSessionKey(uint64_t characterId, const std::array<uint8_t, 32>& key)
	{
		if (characterId == 0u)
		{
			return;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it != m_admitted.end())
		{
			if (!it->second.hasUdpSessionKey || it->second.udpSessionKey != key)
				it->second.usedUdpSalts.clear(); // nouvelle clé : nouveaux sels
			it->second.udpSessionKey = key;
			it->second.hasUdpSessionKey = true;
		}
	}

	AdmittedCharacterRegistry::SaltUse AdmittedCharacterRegistry::ConsumeUdpSalt(uint64_t characterId,
		const std::array<uint8_t, 32>& key, const std::array<uint8_t, 16>& salt, uint64_t nowMs)
	{
		if (characterId == 0u)
		{
			return SaltUse::NoKey;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it == m_admitted.end() || (nowMs - it->second.admittedAtMs) > m_ttlMs
			|| !it->second.hasUdpSessionKey || it->second.udpSessionKey != key)
		{
			return SaltUse::NoKey;
		}
		auto& salts = it->second.usedUdpSalts;
		for (const auto& used : salts)
		{
			if (used == salt)
				return SaltUse::Used;
		}
		if (salts.size() >= kMaxUdpSaltsPerKey)
		{
			return SaltUse::Exhausted;
		}
		salts.push_back(salt);
		return SaltUse::Fresh;
	}

	std::optional<std::array<uint8_t, 32>> AdmittedCharacterRegistry::AdmittedUdpSessionKey(uint64_t characterId,
		uint64_t nowMs) const
	{
		if (characterId == 0u)
		{
			return std::nullopt;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_admitted.find(characterId);
		if (it == m_admitted.end() || (nowMs - it->second.admittedAtMs) > m_ttlMs || !it->second.hasUdpSessionKey)
		{
			return std::nullopt;
		}
		return it->second.udpSessionKey;
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

	uint64_t AdmittedCharacterRegistry::AdmittedGuildId(uint64_t characterId, uint64_t nowMs) const
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
		return it->second.guildId;
	}

	size_t AdmittedCharacterRegistry::Count() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_admitted.size();
	}
}
