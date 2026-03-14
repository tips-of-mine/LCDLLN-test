#pragma once

#include "engine/network/ByteWriter.h"
#include "engine/network/ProtocolV1Constants.h"

#include <cstdint>
#include <vector>

namespace engine::network
{
	/// Builds a protocol v1 packet: reserves header, writes payload, patches size.
	/// Enforces max 16 KB. Conforms to protocol_v1.md.
	class PacketBuilder
	{
	public:
		PacketBuilder();

		/// Returns a ByteWriter for the payload (starting after the 18-byte header). Header is reserved and patched on Finalize().
		ByteWriter PayloadWriter() noexcept;
		/// Patches the header with size (18 + payloadBytesWritten), opcode, flags, requestId, sessionId. Resizes buffer to exact size. Returns false if total > 16 KB.
		bool Finalize(uint16_t opcode, uint16_t flags, uint32_t requestId, uint64_t sessionId, size_t payloadBytesWritten) noexcept;
		/// Returns the built packet (valid after a successful Finalize). Empty if never finalized or Finalize failed.
		const std::vector<uint8_t>& Data() const noexcept { return m_buffer; }
		/// Returns pointer to data and size, for sending. Empty if build failed or not finalized.
		const uint8_t* DataPtr() const noexcept { return m_buffer.empty() ? nullptr : m_buffer.data(); }
		size_t DataSize() const noexcept { return m_buffer.size(); }

	private:
		std::vector<uint8_t> m_buffer;
	};
}
