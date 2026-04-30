#include "engine/network/ChatPayloads.h"

#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"

namespace engine::network
{
	std::optional<ChatSendRequestPayload> ParseChatSendRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ChatSendRequestPayload out;
		uint8_t ch = 0;
		if (!r.ReadBytes(&ch, 1u))
			return std::nullopt;
		out.channel = ch;
		if (!r.ReadString(out.text))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildChatSendRequestPayload(uint8_t channel, std::string_view text)
	{
		// Borne défensive : si l'appelant dépasse le plafond, on tronque pour éviter
		// un Build qui échouerait sur kProtocolV1MaxStringLength.
		if (text.size() > kMaxChatTextBytes)
			text = text.substr(0, kMaxChatTextBytes);

		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&channel, 1u))
			return {};
		if (!w.WriteString(text))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<ChatRelayPayload> ParseChatRelayPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ChatRelayPayload out;
		if (!r.ReadU64(out.timestampUnixMs))
			return std::nullopt;
		uint8_t ch = 0;
		if (!r.ReadBytes(&ch, 1u))
			return std::nullopt;
		out.channel = ch;
		if (!r.ReadString(out.sender))
			return std::nullopt;
		if (!r.ReadString(out.text))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildChatRelayPacket(uint64_t timestampUnixMs, uint8_t channel,
	                                          std::string_view sender, std::string_view text,
	                                          uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(timestampUnixMs))
			return {};
		if (!w.WriteBytes(&channel, 1u))
			return {};
		if (!w.WriteString(sender))
			return {};
		if (!w.WriteString(text))
			return {};
		const size_t payloadBytes = w.Offset();
		// CHAT_RELAY est un push asynchrone serveur → request_id = 0 (cf. RequestResponseDispatcher).
		if (!builder.Finalize(kOpcodeChatRelay, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
