#include "src/masterd/session/SessionCharacterMap.h"

#include <algorithm>

namespace engine::server
{
	std::string SessionCharacterMap::Normalize(const std::string& name)
	{
		std::string out;
		out.reserve(name.size());
		for (char c : name)
		{
			if (c >= 'A' && c <= 'Z')
				out.push_back(static_cast<char>(c - 'A' + 'a'));
			else
				out.push_back(c);
		}
		return out;
	}

	void SessionCharacterMap::Set(uint32_t connId, uint64_t accountId, uint64_t characterId, std::string characterName,
		std::string normalizedName, AccountRole role)
	{
		std::lock_guard lock(m_mutex);

		// Drop previous binding for this connId : retire l'ancien nom -> conn.
		auto itOld = m_byConn.find(connId);
		if (itOld != m_byConn.end())
		{
			auto byNameOld = m_byNormalizedName.find(itOld->second.normalizedName);
			if (byNameOld != m_byNormalizedName.end() && byNameOld->second == connId)
				m_byNormalizedName.erase(byNameOld);
		}

		// Si un autre conn possedait deja ce nom (perso supprime puis recree, ou
		// double-login non-kicke ?) : on remplace, le nouveau owner gagne. Ce cas
		// reste theorique car le master kick deja les sessions duplicate-login.
		auto byNameNew = m_byNormalizedName.find(normalizedName);
		if (byNameNew != m_byNormalizedName.end() && byNameNew->second != connId)
		{
			auto otherConn = byNameNew->second;
			auto itOther = m_byConn.find(otherConn);
			if (itOther != m_byConn.end() && itOther->second.normalizedName == normalizedName)
			{
				// L'autre connId perd son binding nom (mais garde ses character_id pour CHAT_RELAY).
				// On supprime juste l'entree byName ; on le re-ecrit en bas.
			}
		}

		CharacterInfo info{};
		info.accountId = accountId;
		info.characterId = characterId;
		info.characterName = std::move(characterName);
		info.normalizedName = normalizedName;
		info.role = role;
		m_byConn[connId] = std::move(info);
		m_byNormalizedName[normalizedName] = connId;
	}

	void SessionCharacterMap::Remove(uint32_t connId)
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byConn.find(connId);
		if (it == m_byConn.end())
			return;
		auto byName = m_byNormalizedName.find(it->second.normalizedName);
		if (byName != m_byNormalizedName.end() && byName->second == connId)
			m_byNormalizedName.erase(byName);
		m_byConn.erase(it);
	}

	std::optional<SessionCharacterMap::CharacterInfo> SessionCharacterMap::GetByConnId(uint32_t connId) const
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byConn.find(connId);
		if (it == m_byConn.end())
			return std::nullopt;
		return it->second;
	}

	std::optional<uint32_t> SessionCharacterMap::FindConnByNormalizedName(const std::string& normalizedName) const
	{
		std::lock_guard lock(m_mutex);
		auto it = m_byNormalizedName.find(normalizedName);
		if (it == m_byNormalizedName.end())
			return std::nullopt;
		return it->second;
	}

	size_t SessionCharacterMap::Count() const
	{
		std::lock_guard lock(m_mutex);
		return m_byConn.size();
	}

	std::vector<uint64_t> SessionCharacterMap::ListInWorldAccountIds() const
	{
		std::lock_guard lock(m_mutex);
		std::vector<uint64_t> ids;
		ids.reserve(m_byConn.size());
		for (const auto& [connId, info] : m_byConn)
		{
			(void)connId;
			if (info.accountId == 0)
				continue; // binding sans compte connu : ignoré (ne devrait pas arriver post-EnterWorld).
			// Dédup linéaire : le nombre de joueurs en jeu reste petit (un seul connId
			// par compte en pratique, le master kick les duplicate-login).
			if (std::find(ids.begin(), ids.end(), info.accountId) == ids.end())
				ids.push_back(info.accountId);
		}
		return ids;
	}

	SessionCharacterMap::RoleCounts SessionCharacterMap::CountByRole() const
	{
		std::lock_guard lock(m_mutex);
		RoleCounts rc{};
		for (const auto& [connId, info] : m_byConn)
		{
			switch (info.role)
			{
				case AccountRole::Moderator:     ++rc.moderator; break;
				case AccountRole::GameMaster:    ++rc.game_master; break;
				case AccountRole::Administrator: ++rc.administrator; break;
				// Player + Console (sentinel runtime jamais persisté, donc jamais
				// attendu en jeu) : comptés comme player pour que la somme des
				// quatre champs reste égale à Count().
				case AccountRole::Player:
				case AccountRole::Console:       ++rc.player; break;
			}
		}
		return rc;
	}
}
