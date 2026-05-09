#include "engine/network/ShardTicketPayloads.h"
#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"

#include <cstring>

namespace engine::network
{
	std::optional<RequestShardTicketPayload> ParseRequestShardTicketPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 4u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		RequestShardTicketPayload out;
		if (!r.ReadU32(out.target_shard_id))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildRequestShardTicketPayload(uint32_t target_shard_id)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(target_shard_id))
			return {};
		return buf;
	}

	std::optional<ShardTicketData> ParseShardTicketPayload(const uint8_t* payload, size_t payloadSize)
	{
		const size_t expected = kShardTicketIdSize + 8u + 4u + 8u + kShardTicketHmacSize; // 68
		if (payload == nullptr || payloadSize < expected)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ShardTicketData out;
		if (!r.ReadBytes(out.ticket_id.data(), kShardTicketIdSize))
			return std::nullopt;
		if (!r.ReadU64(out.account_id) || !r.ReadU32(out.target_shard_id) || !r.ReadU64(out.expires_at))
			return std::nullopt;
		if (!r.ReadBytes(out.hmac.data(), kShardTicketHmacSize))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildShardTicketPayload(const ShardTicketId& ticket_id, uint64_t account_id,
		uint32_t target_shard_id, uint64_t expires_at, const uint8_t* hmac, size_t hmacSize)
	{
		if (hmac == nullptr || hmacSize != kShardTicketHmacSize)
			return {};
		const size_t total = kShardTicketIdSize + 8u + 4u + 8u + kShardTicketHmacSize;
		std::vector<uint8_t> buf(total, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(ticket_id.data(), kShardTicketIdSize))
			return {};
		if (!w.WriteU64(account_id) || !w.WriteU32(target_shard_id) || !w.WriteU64(expires_at))
			return {};
		if (!w.WriteBytes(hmac, kShardTicketHmacSize))
			return {};
		return buf;
	}

	std::vector<uint8_t> BuildShardTicketResponsePacket(uint32_t requestId, const std::vector<uint8_t>& ticketPayload)
	{
		PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!ticketPayload.empty() && !w.WriteBytes(ticketPayload.data(), ticketPayload.size()))
			return {};
		if (!builder.Finalize(kOpcodeShardTicketResponse, 0, requestId, 0, ticketPayload.size()))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildShardTicketAcceptedPacket(uint32_t requestId)
	{
		PacketBuilder builder;
		if (!builder.Finalize(kOpcodeShardTicketAccepted, 0, requestId, 0, 0))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildShardTicketRejectedPacket(uint32_t requestId, std::string_view reason)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!reason.empty() && !w.WriteString(reason))
			return {};
		size_t payloadLen = w.Offset();
		if (!builder.Finalize(kOpcodeShardTicketRejected, 0, requestId, 0, payloadLen))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildPresentShardTicketPacket(uint32_t requestId, std::span<const uint8_t> ticketPayload)
	{
		PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!ticketPayload.empty() && !w.WriteBytes(ticketPayload.data(), ticketPayload.size()))
			return {};
		if (!builder.Finalize(kOpcodePresentShardTicket, 0, requestId, 0, ticketPayload.size()))
			return {};
		return builder.Data();
	}
}
