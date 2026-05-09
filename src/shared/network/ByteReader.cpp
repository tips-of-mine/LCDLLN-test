#include "engine/network/ByteReader.h"

#include <cstring>

namespace engine::network
{
	ByteReader::ByteReader(const uint8_t* data, size_t size) noexcept
		: m_data(data)
		, m_size(size)
		, m_offset(0)
		, m_ok(true)
	{
	}

	bool ByteReader::ReadU16(uint16_t& out) noexcept
	{
		if (m_offset + 2 > m_size)
		{
			m_ok = false;
			return false;
		}
		out = static_cast<uint16_t>(m_data[m_offset])
			| (static_cast<uint16_t>(m_data[m_offset + 1]) << 8);
		m_offset += 2;
		return true;
	}

	bool ByteReader::ReadU32(uint32_t& out) noexcept
	{
		if (m_offset + 4 > m_size)
		{
			m_ok = false;
			return false;
		}
		out = static_cast<uint32_t>(m_data[m_offset])
			| (static_cast<uint32_t>(m_data[m_offset + 1]) << 8)
			| (static_cast<uint32_t>(m_data[m_offset + 2]) << 16)
			| (static_cast<uint32_t>(m_data[m_offset + 3]) << 24);
		m_offset += 4;
		return true;
	}

	bool ByteReader::ReadU64(uint64_t& out) noexcept
	{
		if (m_offset + 8 > m_size)
		{
			m_ok = false;
			return false;
		}
		out = static_cast<uint64_t>(m_data[m_offset])
			| (static_cast<uint64_t>(m_data[m_offset + 1]) << 8)
			| (static_cast<uint64_t>(m_data[m_offset + 2]) << 16)
			| (static_cast<uint64_t>(m_data[m_offset + 3]) << 24)
			| (static_cast<uint64_t>(m_data[m_offset + 4]) << 32)
			| (static_cast<uint64_t>(m_data[m_offset + 5]) << 40)
			| (static_cast<uint64_t>(m_data[m_offset + 6]) << 48)
			| (static_cast<uint64_t>(m_data[m_offset + 7]) << 56);
		m_offset += 8;
		return true;
	}

	bool ByteReader::ReadBytes(uint8_t* out, size_t count) noexcept
	{
		if (out == nullptr || m_offset + count > m_size)
		{
			m_ok = false;
			return false;
		}
		std::memcpy(out, m_data + m_offset, count);
		m_offset += count;
		return true;
	}

	bool ByteReader::ReadString(std::string& out) noexcept
	{
		uint16_t len16 = 0;
		if (!ReadU16(len16))
			return false;
		const size_t len = static_cast<size_t>(len16);
		if (len > kProtocolV1MaxStringLength || m_offset + len > m_size)
		{
			m_ok = false;
			return false;
		}
		out.assign(reinterpret_cast<const char*>(m_data + m_offset), len);
		m_offset += len;
		return true;
	}

	bool ByteReader::ReadArrayCount(uint16_t& out) noexcept
	{
		return ReadU16(out);
	}
}
