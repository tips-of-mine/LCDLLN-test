// ETAPE3.1 (chantier 4.3) — sessions UDP chiffrées du shard. Voir UdpSessionTable.h.

#include "src/shardd/world/UdpSessionTable.h"

#include <algorithm>
#include <utility>

namespace engine::server
{
	UdpSessionTable::HandshakeResult UdpSessionTable::OpenHandshake(uint64_t endpointKey,
		const engine::network::UdpSessionKey& sessionKey, std::span<const std::byte> datagram, uint64_t nowMs,
		const SaltConsumer& consumeSalt, std::vector<std::byte>& outPlainHello)
	{
		engine::network::UdpHandshakeHeader header{};
		if (!engine::network::PeekUdpHandshake(datagram, header))
			return HandshakeResult::AuthFailed;

		std::lock_guard<std::mutex> lock(m_mutex);
		const auto existing = m_sessions.find(endpointKey);
		if (existing != m_sessions.end())
		{
			if (existing->second.characterId != header.characterId)
			{
				// Un point de terminaison lié à un personnage n'est jamais repris par un
				// autre : sinon un joueur qui usurpe l'adresse source d'un autre (avec sa
				// propre clé, valide) détournerait sa session. La session se libère à la
				// déconnexion ou après inactivité.
				return HandshakeResult::EndpointBusy;
			}
			if (existing->second.crypto.Salt() == header.salt)
				return HandshakeResult::Duplicate;
		}

		Session session{};
		session.characterId = header.characterId;
		session.lastActivityMs = nowMs;
		if (!session.crypto.Init(sessionKey, header.salt, engine::network::UdpSecureRole::Server)
			|| !session.crypto.OpenHandshake(datagram, outPlainHello))
		{
			// Sel NON consommé : un faux Hello ne doit pas bloquer le vrai.
			return HandshakeResult::AuthFailed;
		}
		if (!consumeSalt || !consumeSalt(header.salt))
		{
			outPlainHello.clear();
			return HandshakeResult::Replayed;
		}
		m_sessions[endpointKey] = std::move(session);
		return HandshakeResult::Opened;
	}

	bool UdpSessionTable::OpenData(uint64_t endpointKey, std::span<const std::byte> datagram, uint64_t nowMs,
		std::vector<std::byte>& outPlain)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_sessions.find(endpointKey);
		if (it == m_sessions.end() || !it->second.crypto.Open(datagram, outPlain))
			return false;
		it->second.lastActivityMs = nowMs;
		return true;
	}

	UdpSessionTable::SealResult UdpSessionTable::Seal(uint64_t endpointKey, std::span<const std::byte> plain,
		std::vector<std::byte>& outDatagram)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_sessions.find(endpointKey);
		if (it == m_sessions.end())
			return SealResult::NoSession;
		return it->second.crypto.Seal(plain, outDatagram) ? SealResult::Sealed : SealResult::Failed;
	}

	bool UdpSessionTable::HasSession(uint64_t endpointKey) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_sessions.find(endpointKey) != m_sessions.end();
	}

	uint64_t UdpSessionTable::SessionCharacterId(uint64_t endpointKey) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_sessions.find(endpointKey);
		return it == m_sessions.end() ? 0u : it->second.characterId;
	}

	void UdpSessionTable::Drop(uint64_t endpointKey)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sessions.erase(endpointKey);
	}

	void UdpSessionTable::Prune(uint64_t nowMs, uint64_t idleMs, const std::function<bool(uint64_t endpointKey)>& inUse)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_sessions.begin(); it != m_sessions.end();)
		{
			const bool used = inUse && inUse(it->first);
			if (!used && nowMs - it->second.lastActivityMs >= idleMs)
				it = m_sessions.erase(it);
			else
				++it;
		}
	}

	void UdpSessionTable::Clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sessions.clear();
	}

	size_t UdpSessionTable::SessionCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_sessions.size();
	}
}
