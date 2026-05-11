// Wave 12 — PacketLog : implementation du ring buffer de paquets recents.
// Voir PacketLog.h pour la conception. Aucun appel a un service externe :
// le caller fournit le timestamp courant pour decoupler des syscalls.

#include "src/shared/network/PacketLog.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

namespace engine::server::netdebug
{
	namespace
	{
		/// Formatte un timestamp Unix (millisecondes UTC) en
		/// "YYYY-MM-DD HH:MM:SS.mmm". gmtime_s sous MSVC, gmtime_r ailleurs.
		/// Effet de bord : aucun (pure conversion).
		std::string FormatTimestampUtc(uint64_t timestampUnixMs)
		{
			const time_t secs = static_cast<time_t>(timestampUnixMs / 1000ULL);
			const unsigned int millis =
				static_cast<unsigned int>(timestampUnixMs % 1000ULL);

			std::tm tmUtc{};
#if defined(_WIN32)
			gmtime_s(&tmUtc, &secs);
#else
			gmtime_r(&secs, &tmUtc);
#endif

			char buf[32] = {0};
			// "YYYY-MM-DD HH:MM:SS" tient sur 19 octets + NUL.
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmUtc);

			char out[40] = {0};
			std::snprintf(out, sizeof(out), "%s.%03u", buf, millis);
			return std::string(out);
		}

		/// Formatte les premiers \p len octets de \p preview en hex
		/// majuscule, separes par des espaces (ex: "AA BB CC"). Borne par
		/// kPreviewBytes pour ne pas deborder.
		std::string FormatPreviewHex(const std::array<uint8_t, kPreviewBytes>& preview,
		                             size_t len)
		{
			const size_t n = std::min(len, kPreviewBytes);
			if (n == 0)
				return std::string();

			std::string out;
			out.reserve(n * 3);
			char tmp[4];
			for (size_t i = 0; i < n; ++i)
			{
				std::snprintf(tmp, sizeof(tmp), "%02X", preview[i]);
				if (i > 0)
					out.push_back(' ');
				out.append(tmp);
			}
			return out;
		}

		/// Convertit l'enum PacketDirection en libelle court "RX"/"TX".
		const char* DirectionLabel(PacketDirection dir) noexcept
		{
			return (dir == PacketDirection::TX) ? "TX" : "RX";
		}
	}

	PacketLog::PacketLog(size_t capacity)
		: m_capacity(capacity)
	{
		// Pre-allocation : on dimensionne le vector a la capacite max
		// pour que Append soit O(1) sans realloc. Si capacity == 0, le
		// log est inerte (Append/Drain no-op).
		if (m_capacity > 0)
			m_ring.resize(m_capacity);
	}

	void PacketLog::Append(PacketDirection dir,
	                       uint32_t connId,
	                       uint16_t opcode,
	                       uint32_t requestId,
	                       uint64_t sessionId,
	                       const uint8_t* payload,
	                       size_t payloadSize,
	                       uint64_t nowUnixMs)
	{
		if (m_capacity == 0)
			return; // Log inerte.

		PacketLogEntry entry;
		entry.timestampUnixMs = nowUnixMs;
		entry.connId          = connId;
		entry.opcode          = opcode;
		entry.requestId       = requestId;
		entry.sessionId       = sessionId;
		entry.direction       = dir;
		entry.payloadSize     = payloadSize;

		// Copie tronquee du payload (zero-fill restant via array{} init).
		const size_t toCopy = (payload != nullptr)
			? std::min(payloadSize, kPreviewBytes)
			: 0;
		if (toCopy > 0)
			std::memcpy(entry.payloadPreview.data(), payload, toCopy);

		std::lock_guard<std::mutex> lock(m_mu);
		m_ring[m_writeIdx] = entry;
		m_writeIdx = (m_writeIdx + 1) % m_capacity;
		if (m_writeIdx == 0)
			m_wrapped = true;
	}

	std::vector<PacketLogEntry> PacketLog::Drain() const
	{
		std::lock_guard<std::mutex> lock(m_mu);
		if (m_capacity == 0)
			return {};

		std::vector<PacketLogEntry> out;
		if (!m_wrapped)
		{
			// Pas encore wrappe : ordre chronologique = [0..m_writeIdx).
			out.reserve(m_writeIdx);
			for (size_t i = 0; i < m_writeIdx; ++i)
				out.push_back(m_ring[i]);
		}
		else
		{
			// Wrappe : plus vieux en [m_writeIdx..capacity), puis [0..m_writeIdx).
			out.reserve(m_capacity);
			for (size_t i = m_writeIdx; i < m_capacity; ++i)
				out.push_back(m_ring[i]);
			for (size_t i = 0; i < m_writeIdx; ++i)
				out.push_back(m_ring[i]);
		}
		return out;
	}

	std::vector<PacketLogEntry> PacketLog::DrainForConn(uint32_t connId) const
	{
		auto all = Drain();
		std::vector<PacketLogEntry> filtered;
		filtered.reserve(all.size());
		for (const auto& e : all)
		{
			if (e.connId == connId)
				filtered.push_back(e);
		}
		return filtered;
	}

	size_t PacketLog::Size() const
	{
		std::lock_guard<std::mutex> lock(m_mu);
		if (m_capacity == 0)
			return 0;
		return m_wrapped ? m_capacity : m_writeIdx;
	}

	std::string FormatEntries(const std::vector<PacketLogEntry>& entries)
	{
		if (entries.empty())
			return std::string();

		std::ostringstream oss;
		for (const auto& e : entries)
		{
			oss << '[' << FormatTimestampUtc(e.timestampUnixMs) << "] "
			    << DirectionLabel(e.direction) << ' '
			    << "connId=" << e.connId << ' '
			    << "opcode=" << e.opcode << ' '
			    << "reqId="  << e.requestId << ' '
			    << "sessId=" << e.sessionId << ' '
			    << "size="   << e.payloadSize;

			const std::string hex = FormatPreviewHex(e.payloadPreview, e.payloadSize);
			if (!hex.empty())
				oss << " preview=" << hex;
			oss << '\n';
		}
		return oss.str();
	}
}
