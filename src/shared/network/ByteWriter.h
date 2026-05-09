#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace engine::network
{
	/// Little-endian byte writer. Bounds-checked; never writes past the buffer.
	/// Conforms to protocol_v1.md (field-by-field, no packed struct).
	class ByteWriter
	{
	public:
		/// \param data Start of the writable buffer.
		/// \param size Size of the buffer in bytes.
		ByteWriter(uint8_t* data, size_t size) noexcept;

		/// Returns the number of bytes written so far.
		size_t Offset() const noexcept { return m_offset; }
		/// Returns the total buffer size.
		size_t Capacity() const noexcept { return m_size; }
		/// Returns the number of bytes remaining (capacity - offset).
		size_t Remaining() const noexcept { return m_size - m_offset; }
		/// Returns true if no write has failed (offset unchanged after a failed write).
		bool Ok() const noexcept { return m_ok; }

		/// Writes a uint16 in little-endian. Returns true on success, false if not enough space.
		bool WriteU16(uint16_t value) noexcept;
		/// Writes a uint32 in little-endian. Returns true on success.
		bool WriteU32(uint32_t value) noexcept;
		/// Writes a uint64 in little-endian. Returns true on success.
		bool WriteU64(uint64_t value) noexcept;
		/// Writes raw bytes. Returns true on success.
		bool WriteBytes(const uint8_t* src, size_t count) noexcept;
		/// Writes a protocol v1 string: uint16 length (byte count) then UTF-8 bytes. Fails if length > kProtocolV1MaxStringLength or not enough space.
		bool WriteString(std::string_view str) noexcept;
		/// Writes the array count prefix (uint16). Caller then writes \a count elements. Returns true on success.
		bool WriteArrayCount(uint16_t count) noexcept;

	private:
		uint8_t* m_data;
		size_t m_size;
		size_t m_offset;
		bool m_ok;
	};
}
