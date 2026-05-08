#include "engine/core/util/ByteBuffer.h"

#include <cstring>

namespace engine::core::util
{
	ByteBuffer::ByteBuffer() = default;

	ByteBuffer::ByteBuffer(std::size_t reserve)
	{
		m_data.reserve(reserve);
	}

	ByteBuffer& ByteBuffer::operator<<(std::string_view s)
	{
		const std::size_t len = s.size();
		// Garde-fou : on refuse les strings > 65535 octets en marquant erreur.
		// Un appelant qui sérialise une string aussi grosse a un bug — il devra
		// utiliser un autre canal (préfixe uint32 ad hoc).
		if (len > kMaxStringLength)
		{
			m_error = true;
			return *this;
		}
		const std::uint16_t len16 = static_cast<std::uint16_t>(len);
		AppendArithmetic(len16);
		if (len > 0)
		{
			AppendBytesRaw(reinterpret_cast<const std::uint8_t*>(s.data()), len);
		}
		return *this;
	}

	ByteBuffer& ByteBuffer::operator>>(std::string& out)
	{
		AlignReadToByte();
		std::uint16_t len = 0;
		ReadArithmetic(len);
		if (m_error)
		{
			out.clear();
			return *this;
		}
		if (m_readPos + len > m_writePos)
		{
			m_error = true;
			out.clear();
			return *this;
		}
		out.assign(reinterpret_cast<const char*>(m_data.data() + m_readPos), len);
		m_readPos += len;
		return *this;
	}

	void ByteBuffer::AppendBytes(const std::uint8_t* src, std::size_t count)
	{
		if (m_bitWritePos != 0)
		{
			FlushBits();
		}
		AppendBytesRaw(src, count);
	}

	void ByteBuffer::AppendBytesRaw(const std::uint8_t* src, std::size_t count)
	{
		if (count == 0)
		{
			return;
		}
		const std::size_t end = m_writePos + count;
		if (end > m_data.size())
		{
			m_data.resize(end);
		}
		std::memcpy(m_data.data() + m_writePos, src, count);
		m_writePos = end;
	}

	bool ByteBuffer::ReadBytes(std::uint8_t* dst, std::size_t count)
	{
		AlignReadToByte();
		if (m_readPos + count > m_writePos)
		{
			m_error = true;
			return false;
		}
		if (count > 0)
		{
			std::memcpy(dst, m_data.data() + m_readPos, count);
			m_readPos += count;
		}
		return true;
	}

	void ByteBuffer::WriteBit(bool b)
	{
		if (b)
		{
			m_bitWriteBuf |= static_cast<std::uint8_t>(1u << m_bitWritePos);
		}
		++m_bitWritePos;
		if (m_bitWritePos == 8)
		{
			AppendBytesRaw(&m_bitWriteBuf, 1);
			m_bitWriteBuf = 0;
			m_bitWritePos = 0;
		}
	}

	void ByteBuffer::FlushBits()
	{
		if (m_bitWritePos == 0)
		{
			return;
		}
		AppendBytesRaw(&m_bitWriteBuf, 1);
		m_bitWriteBuf = 0;
		m_bitWritePos = 0;
	}

	bool ByteBuffer::ReadBit()
	{
		if (m_bitReadPos >= 8)
		{
			if (m_readPos + 1 > m_writePos)
			{
				m_error = true;
				return false;
			}
			m_bitReadBuf = m_data[m_readPos++];
			m_bitReadPos = 0;
		}
		const bool b = ((m_bitReadBuf >> m_bitReadPos) & 0x1u) != 0;
		++m_bitReadPos;
		return b;
	}

	void ByteBuffer::AlignReadToByte() noexcept
	{
		// Bascule le sub-buffer de bits comme "vide" pour que le prochain
		// ReadBit recharge un nouvel octet. Les bits restants de l'octet
		// courant sont abandonnés silencieusement — c'est documenté dans le
		// header (mélanger reads bit/byte sans align explicite est un bug
		// appelant).
		m_bitReadPos = 8;
	}

	void ByteBuffer::SeekRead(std::size_t pos) noexcept
	{
		m_readPos = pos;
		m_bitReadPos = 8;
	}

	void ByteBuffer::SeekWrite(std::size_t pos)
	{
		FlushBits();
		if (pos > m_data.size())
		{
			m_data.resize(pos);
		}
		m_writePos = pos;
	}

	std::span<const std::uint8_t> ByteBuffer::Data() const noexcept
	{
		return std::span<const std::uint8_t>(m_data.data(), m_writePos);
	}

	void ByteBuffer::Clear() noexcept
	{
		m_data.clear();
		m_writePos = 0;
		m_readPos = 0;
		m_error = false;
		m_bitWriteBuf = 0;
		m_bitWritePos = 0;
		m_bitReadBuf = 0;
		m_bitReadPos = 8;
	}
}
