#include "src/masterd/chat/ChatChannelRegistry.h"

#include <algorithm>
#include <cctype>

namespace engine::server::chat
{
	namespace
	{
		constexpr size_t kMinNameLen = 1;
		constexpr size_t kMaxNameLen = 32;

		/// Caractère ASCII printable non-espace, non-'/' ?
		bool IsValidNameChar(char c) noexcept
		{
			const auto u = static_cast<unsigned char>(c);
			if (u <= 0x20 || u >= 0x7F) return false; // ASCII printable strict
			if (c == '/') return false;
			return true;
		}
	}

	std::string ChatChannelRegistry::NormalizeName(std::string_view name)
	{
		if (name.size() < kMinNameLen || name.size() > kMaxNameLen)
			return {};
		std::string out;
		out.reserve(name.size());
		for (char c : name)
		{
			if (!IsValidNameChar(c))
				return {};
			const auto u = static_cast<unsigned char>(c);
			if (u >= 'A' && u <= 'Z')
				out.push_back(static_cast<char>(u + ('a' - 'A')));
			else
				out.push_back(c);
		}
		return out;
	}

	ChannelJoinResult ChatChannelRegistry::Join(uint64_t accountId,
		std::string_view channel, std::string_view password)
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return ChannelJoinResult::InvalidName;

		std::lock_guard<std::mutex> lk(m_mutex);

		auto it = m_channels.find(key);
		if (it == m_channels.end())
		{
			// Création : accountId devient owner. Pas de password initial
			// (l'owner peut le set ensuite via SetPassword). Pas de bans.
			Channel ch;
			ch.name  = key;
			ch.owner = accountId;
			ch.members.insert(accountId);
			m_channels.emplace(key, std::move(ch));
			return ChannelJoinResult::OK;
		}

		Channel& ch = it->second;
		if (ch.bans.count(accountId) != 0)
			return ChannelJoinResult::Banned;

		if (!ch.password.empty() && password != ch.password)
			return ChannelJoinResult::WrongPassword;

		const auto inserted = ch.members.insert(accountId).second;
		return inserted ? ChannelJoinResult::OK : ChannelJoinResult::AlreadyMember;
	}

	void ChatChannelRegistry::Leave(uint64_t accountId, std::string_view channel)
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return;

		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return;
		Channel& ch = it->second;
		ch.members.erase(accountId);
		if (ch.members.empty())
		{
			m_channels.erase(it);
			return;
		}
		// Transfert d'ownership si owner sortant.
		if (ch.owner == accountId)
		{
			// Premier membre dans l'unordered_set (ordre non-stable mais
			// déterministe pour une session donnée).
			ch.owner = *ch.members.begin();
		}
	}

	void ChatChannelRegistry::LeaveAll(uint64_t accountId)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		// Itère + collecte les canaux à supprimer pour ne pas invalider
		// l'itérateur principal pendant l'effacement.
		for (auto it = m_channels.begin(); it != m_channels.end(); )
		{
			Channel& ch = it->second;
			ch.members.erase(accountId);
			if (ch.members.empty())
			{
				it = m_channels.erase(it);
				continue;
			}
			if (ch.owner == accountId)
				ch.owner = *ch.members.begin();
			++it;
		}
	}

	std::vector<uint64_t> ChatChannelRegistry::Members(std::string_view channel) const
	{
		std::vector<uint64_t> out;
		const auto key = NormalizeName(channel);
		if (key.empty())
			return out;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return out;
		out.reserve(it->second.members.size());
		for (auto id : it->second.members)
			out.push_back(id);
		return out;
	}

	bool ChatChannelRegistry::IsMember(uint64_t accountId, std::string_view channel) const
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return false;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return false;
		return it->second.members.count(accountId) != 0;
	}

	bool ChatChannelRegistry::SetPassword(uint64_t actor, std::string_view channel,
		std::string_view password)
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return false;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return false;
		if (it->second.owner != actor)
			return false;
		it->second.password = std::string(password);
		return true;
	}

	bool ChatChannelRegistry::Ban(uint64_t actor, std::string_view channel, uint64_t target)
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return false;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return false;
		Channel& ch = it->second;
		if (ch.owner != actor)
			return false;
		// L'owner ne peut pas se ban lui-même (no-op).
		if (target == actor)
			return false;
		ch.bans.insert(target);
		ch.members.erase(target);
		// Si on bannit le seul autre membre alors qu'on est l'owner, on
		// reste seul ; ne pas effacer le canal (l'owner est encore là).
		return true;
	}

	bool ChatChannelRegistry::Unban(uint64_t actor, std::string_view channel, uint64_t target)
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return false;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return false;
		if (it->second.owner != actor)
			return false;
		it->second.bans.erase(target);
		return true;
	}

	bool ChatChannelRegistry::IsBanned(uint64_t accountId, std::string_view channel) const
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return false;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return false;
		return it->second.bans.count(accountId) != 0;
	}

	std::optional<ChannelInfo> ChatChannelRegistry::Info(std::string_view channel) const
	{
		const auto key = NormalizeName(channel);
		if (key.empty())
			return std::nullopt;
		std::lock_guard<std::mutex> lk(m_mutex);
		auto it = m_channels.find(key);
		if (it == m_channels.end())
			return std::nullopt;
		ChannelInfo info;
		info.name           = it->second.name;
		info.ownerAccountId = it->second.owner;
		info.hasPassword    = !it->second.password.empty();
		info.memberCount    = it->second.members.size();
		return info;
	}

	size_t ChatChannelRegistry::ChannelCount() const
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		return m_channels.size();
	}
}
