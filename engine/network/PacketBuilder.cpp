#include "engine/network/PacketBuilder.h"

namespace engine::network
{
	PacketBuilder::PacketBuilder()
	{
		m_buffer.resize(kProtocolV1MaxPacketSize, 0u);
	}

	ByteWriter PacketBuilder::PayloadWriter() noexcept
	{
		if (m_buffer.size() < kProtocolV1HeaderSize)
			return ByteWriter(nullptr, 0);
		return ByteWriter(m_buffer.data() + kProtocolV1HeaderSize, m_buffer.size() - kProtocolV1HeaderSize);
	}

	bool PacketBuilder::Finalize(uint16_t opcode, uint16_t flags, uint32_t requestId, uint64_t sessionId, size_t payloadBytesWritten) noexcept
	{
		const size_t totalSize = kProtocolV1HeaderSize + payloadBytesWritten;
		if (totalSize > kProtocolV1MaxPacketSize)
			return false;
		m_buffer.resize(totalSize);
		const uint16_t size16 = static_cast<uint16_t>(totalSize);
		ByteWriter header(m_buffer.data(), kProtocolV1HeaderSize);
		if (!header.WriteU16(size16) || !header.WriteU16(opcode) || !header.WriteU16(flags)
			|| !header.WriteU32(requestId) || !header.WriteU64(sessionId))
			return false;
		return true;
	}
}
