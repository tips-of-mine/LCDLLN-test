#pragma once
// CMANGOS.12 (Phase 4.12a) — PacketLog : ring buffer in-memory des derniers
// paquets recus/envoyes par session, pour debug/post-mortem. Header-only.
//
// Usage : sur un crash ou un kick suspect, dump du PacketLog d'une session
// dans le crash report. Volontairement simple : pas de fichier disque, pas
// de filtrage runtime — c'est un buffer pour observation, pas de l'audit.

#include <cstdint>
#include <cstring>
#include <vector>

namespace engine::server::packetlog
{
	enum class Direction : uint8_t { In, Out };

	struct Entry
	{
		uint64_t  tsMs    = 0;
		uint16_t  opcode  = 0;
		uint32_t  size    = 0;
		Direction dir     = Direction::In;
		// On conserve un prefix de payload (premieres N octets) pour debug
		// sans peter la memoire. Reste tronque dans Push.
		std::vector<uint8_t> prefix;
	};

	class PacketLog
	{
	public:
		explicit PacketLog(size_t capacity = 256, size_t prefixBytes = 32)
			: m_cap(capacity), m_prefixBytes(prefixBytes)
		{
			m_ring.reserve(capacity);
		}

		void Push(Direction dir, uint16_t opcode, uint64_t tsMs,
		          const uint8_t* data, uint32_t size)
		{
			Entry e;
			e.tsMs   = tsMs;
			e.opcode = opcode;
			e.size   = size;
			e.dir    = dir;
			const uint32_t take = (size < m_prefixBytes) ? size : static_cast<uint32_t>(m_prefixBytes);
			e.prefix.assign(data, data + take);

			if (m_ring.size() < m_cap)
			{
				m_ring.push_back(std::move(e));
			}
			else
			{
				m_ring[m_head] = std::move(e);
			}
			m_head = (m_head + 1) % m_cap;
		}

		size_t Size() const { return m_ring.size(); }
		size_t Capacity() const { return m_cap; }

		/// Snapshot dans l'ordre chronologique (du plus ancien au plus recent).
		std::vector<Entry> Snapshot() const
		{
			std::vector<Entry> out;
			out.reserve(m_ring.size());
			if (m_ring.size() < m_cap)
			{
				out = m_ring;
				return out;
			}
			// buffer plein : tete = oldest
			for (size_t i = 0; i < m_ring.size(); ++i)
			{
				out.push_back(m_ring[(m_head + i) % m_cap]);
			}
			return out;
		}

	private:
		size_t  m_cap;
		size_t  m_prefixBytes;
		size_t  m_head = 0;     ///< prochain slot a ecrire (et donc oldest si plein)
		std::vector<Entry> m_ring;
	};
}
