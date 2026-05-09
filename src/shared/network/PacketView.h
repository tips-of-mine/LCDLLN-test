#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <cstddef>
#include <cstdint>

namespace engine::network
{
	/// Result of parsing a packet from a buffer.
	enum class PacketParseResult
	{
		Ok,        ///< Valid packet; payload exposed.
		Incomplete,///< Need more data (announced size > bytes available, size <= 16 KB).
		Invalid    ///< Invalid (size < header or size > 16 KB).
	};

	/// Read-only view of a parsed protocol v1 packet. Validates size (>= 18, <= 16 KB) and exposes payload slice.
	/// If bytes_available < announced size, returns Incomplete (framing: wait for more data).
	class PacketView
	{
	public:
		/// Parses the header from \a data (at least \a bytesAvailable bytes). Does not copy; view is valid as long as \a data is valid.
		/// \return Ok: full packet in buffer, Payload() and header accessors valid. Incomplete: need more bytes. Invalid: size invalid or > 16 KB.
		static PacketParseResult Parse(const uint8_t* data, size_t bytesAvailable, PacketView& out) noexcept;

		/// Total packet size (from header).
		uint16_t Size() const noexcept { return m_size; }
		uint16_t Opcode() const noexcept { return m_opcode; }
		uint16_t Flags() const noexcept { return m_flags; }
		uint32_t RequestId() const noexcept { return m_requestId; }
		uint64_t SessionId() const noexcept { return m_sessionId; }
		/// Payload pointer (data + 18). Valid only if Parse returned Ok.
		const uint8_t* Payload() const noexcept { return m_payload; }
		/// Payload size in bytes (Size() - 18).
		size_t PayloadSize() const noexcept { return m_payloadSize; }

		/// Default constructor; use Parse() to populate.
		PacketView() = default;

	private:
		uint16_t m_size = 0;
		uint16_t m_opcode = 0;
		uint16_t m_flags = 0;
		uint32_t m_requestId = 0;
		uint64_t m_sessionId = 0;
		const uint8_t* m_payload = nullptr;
		size_t m_payloadSize = 0;
	};
}
