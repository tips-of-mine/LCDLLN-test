#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::network
{
	/// Little-endian byte reader. Bounds-checked; never reads past the buffer.
	/// Every read returns a status (ok/fail); on fail the reader state is unchanged for the failed read.
	class ByteReader
	{
	public:
		/// \param data Start of the readable buffer.
		/// \param size Size of the buffer in bytes.
		ByteReader(const uint8_t* data, size_t size) noexcept;

		/// Current read position in bytes.
		size_t Offset() const noexcept { return m_offset; }
		/// Total buffer size.
		size_t Size() const noexcept { return m_size; }
		/// Bytes remaining from current position to end.
		size_t Remaining() const noexcept { return m_offset <= m_size ? m_size - m_offset : 0u; }
		/// True if no read has failed.
		bool Ok() const noexcept { return m_ok; }

		/// Reads a uint16 little-endian. Returns true on success.
		bool ReadU16(uint16_t& out) noexcept;
		/// Reads a uint32 little-endian. Returns true on success.
		bool ReadU32(uint32_t& out) noexcept;
		/// Reads a uint64 little-endian. Returns true on success.
		bool ReadU64(uint64_t& out) noexcept;
		/// Reads raw bytes into \a out (must have at least \a count bytes). Returns true on success.
		bool ReadBytes(uint8_t* out, size_t count) noexcept;
		/// Reads a protocol v1 string (uint16 length + UTF-8 bytes). Fails if length > kProtocolV1MaxStringLength or OOB. Returns true on success.
		bool ReadString(std::string& out) noexcept;
		/// Reads the array count prefix (uint16). Returns true on success.
		bool ReadArrayCount(uint16_t& out) noexcept;

	private:
		const uint8_t* m_data;
		size_t m_size;
		size_t m_offset;
		bool m_ok;
	};
}
