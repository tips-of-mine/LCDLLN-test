#include "engine/network/ServerListPayloads.h"
#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"

#include <cstring>

namespace engine::network
{
	std::vector<ServerListEntry> ParseServerListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		std::vector<ServerListEntry> out;
		if (payload == nullptr || payloadSize < 2u)
			return out;
		ByteReader r(payload, payloadSize);
		uint16_t count = 0;
		if (!r.ReadU16(count))
			return out;
		out.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count && r.Remaining() >= 17u; ++i)
		{
			ServerListEntry e;
			if (!r.ReadU32(e.shard_id))
				break;
			uint8_t statusByte = 0;
			if (!r.ReadBytes(&statusByte, 1))
				break;
			e.status = statusByte;
			if (!r.ReadU32(e.current_load) || !r.ReadU32(e.max_capacity) || !r.ReadU32(e.character_count))
				break;
			if (!r.ReadString(e.endpoint))
				break;
			out.push_back(e);
		}
		return out;
	}

	std::vector<uint8_t> BuildServerListResponsePayload(const std::vector<ServerListEntry>& entries)
	{
		size_t total = 2u;
		for (const auto& e : entries)
			total += 4u + 1u + 4u + 4u + 4u + 2u + e.endpoint.size();
		if (total > kProtocolV1MaxPacketSize)
			return {};
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU16(static_cast<uint16_t>(entries.size())))
			return {};
		for (const auto& e : entries)
		{
			if (!w.WriteU32(e.shard_id))
				return {};
			uint8_t st = e.status;
			if (!w.WriteBytes(&st, 1))
				return {};
			if (!w.WriteU32(e.current_load) || !w.WriteU32(e.max_capacity) || !w.WriteU32(e.character_count))
				return {};
			if (!w.WriteString(e.endpoint))
				return {};
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildServerListResponsePacket(uint32_t requestId, const std::vector<uint8_t>& payload)
	{
		PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!payload.empty() && !w.WriteBytes(payload.data(), payload.size()))
			return {};
		if (!builder.Finalize(kOpcodeServerListResponse, 0, requestId, 0, payload.size()))
			return {};
		return builder.Data();
	}
}
