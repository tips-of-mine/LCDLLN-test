#include "engine/network/ByteWriter.h"

#include <cstring>

namespace engine::network
{
	ByteWriter::ByteWriter(uint8_t* data, size_t size) noexcept
		: m_data(data)
		, m_size(size)
		, m_offset(0)
		, m_ok(true)
	{
	}

	bool ByteWriter::WriteU16(uint16_t value) noexcept
	{
		if (m_offset + 2 > m_size)
		{
			m_ok = false;
			return false;
		}
		m_data[m_offset + 0] = static_cast<uint8_t>(value & 0xFFu);
		m_data[m_offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
		m_offset += 2;
		return true;
	}

	bool ByteWriter::WriteU32(uint32_t value) noexcept
	{
		if (m_offset + 4 > m_size)
		{
			m_ok = false;
			return false;
		}
		m_data[m_offset + 0] = static_cast<uint8_t>(value & 0xFFu);
		m_data[m_offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
		m_data[m_offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
		m_data[m_offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
		m_offset += 4;
		return true;
	}

	bool ByteWriter::WriteU64(uint64_t value) noexcept
	{
		if (m_offset + 8 > m_size)
		{
			m_ok = false;
			return false;
		}
		m_data[m_offset + 0] = static_cast<uint8_t>(value & 0xFFu);
		m_data[m_offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
		m_data[m_offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
		m_data[m_offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
		m_data[m_offset + 4] = static_cast<uint8_t>((value >> 32) & 0xFFu);
		m_data[m_offset + 5] = static_cast<uint8_t>((value >> 40) & 0xFFu);
		m_data[m_offset + 6] = static_cast<uint8_t>((value >> 48) & 0xFFu);
		m_data[m_offset + 7] = static_cast<uint8_t>((value >> 56) & 0xFFu);
		m_offset += 8;
		return true;
	}

	bool ByteWriter::WriteBytes(const uint8_t* src, size_t count) noexcept
	{
		if (count == 0u)
			return true;
		if (src == nullptr || m_offset + count > m_size)
		{
			m_ok = false;
			return false;
		}
		std::memcpy(m_data + m_offset, src, count);
		m_offset += count;
		return true;
	}

	bool ByteWriter::WriteString(std::string_view str) noexcept
	{
		const size_t len = str.size();
		if (len > kProtocolV1MaxStringLength)
		{
			m_ok = false;
			return false;
		}
		const uint16_t len16 = static_cast<uint16_t>(len);
		if (!WriteU16(len16))
			return false;
		if (len > 0 && !WriteBytes(reinterpret_cast<const uint8_t*>(str.data()), len))
			return false;
		return true;
	}

	bool ByteWriter::WriteArrayCount(uint16_t count) noexcept
	{
		return WriteU16(count);
	}
}
