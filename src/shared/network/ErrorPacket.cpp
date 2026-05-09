// Implémentation de BuildErrorPacket et ParseErrorPayload (voir ErrorPacket.h).
// BuildErrorPacket utilise PacketBuilder pour produire un paquet complet avec en-tête v1.
// ParseErrorPayload lit séquentiellement via ByteReader ; tout buffer < 4 octets est rejeté.
#include "engine/network/ErrorPacket.h"
#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"

#include "engine/core/Log.h"

namespace engine::network
{
	std::vector<uint8_t> BuildErrorPacket(NetErrorCode errorCode, std::string_view message,
		uint32_t requestId, uint64_t sessionId)
	{
		if (message.size() > kProtocolV1MaxStringLength)
		{
			LOG_WARN(Net, "[ErrorPacket] BuildErrorPacket: message too long ({}), truncated", message.size());
			message = message.substr(0, kProtocolV1MaxStringLength);
		}
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		const uint32_t code = static_cast<uint32_t>(errorCode);
		if (!w.WriteU32(code))
			return {};
		if (!w.WriteString(message))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeError, 0, requestId, sessionId, payloadBytes))
			return {};
		return builder.Data();
	}

	std::optional<ErrorPayload> ParseErrorPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 4u)
			return std::nullopt;
		ByteReader reader(payload, payloadSize);
		uint32_t code = 0;
		if (!reader.ReadU32(code))
			return std::nullopt;
		std::string message;
		if (reader.Remaining() >= 2u && !reader.ReadString(message))
			return std::nullopt;
		ErrorPayload out;
		out.errorCode = static_cast<NetErrorCode>(code);
		out.message = std::move(message);
		return out;
	}
}
