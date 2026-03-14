#include "engine/network/PacketView.h"
#include "engine/network/ByteReader.h"

namespace engine::network
{
	PacketParseResult PacketView::Parse(const uint8_t* data, size_t bytesAvailable, PacketView& out) noexcept
	{
		out = PacketView();
		if (data == nullptr || bytesAvailable < kProtocolV1HeaderSize)
			return PacketParseResult::Incomplete;
		ByteReader reader(data, bytesAvailable);
		uint16_t size16 = 0;
		if (!reader.ReadU16(size16))
			return PacketParseResult::Incomplete;
		const size_t size = static_cast<size_t>(size16);
		if (size < kProtocolV1HeaderSize)
			return PacketParseResult::Invalid;
		if (size > kProtocolV1MaxPacketSize)
			return PacketParseResult::Invalid;
		if (bytesAvailable < size)
			return PacketParseResult::Incomplete;
		uint16_t opcode = 0, flags = 0;
		uint32_t requestId = 0;
		uint64_t sessionId = 0;
		if (!reader.ReadU16(opcode) || !reader.ReadU16(flags) || !reader.ReadU32(requestId) || !reader.ReadU64(sessionId))
			return PacketParseResult::Invalid;
		out.m_size = size16;
		out.m_opcode = opcode;
		out.m_flags = flags;
		out.m_requestId = requestId;
		out.m_sessionId = sessionId;
		out.m_payload = data + kProtocolV1HeaderSize;
		out.m_payloadSize = size - kProtocolV1HeaderSize;
		return PacketParseResult::Ok;
	}
}
