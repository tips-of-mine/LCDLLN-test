#include "engine/network/CharacterPayloads.h"

#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"

namespace engine::network
{
	std::optional<CharacterCreateRequestPayload> ParseCharacterCreateRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterCreateRequestPayload out;
		if (!r.ReadString(out.name))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCharacterCreateRequestPayload(std::string_view name)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(name))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<CharacterCreateResponsePayload> ParseCharacterCreateResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterCreateResponsePayload out;
		if (!r.ReadBytes(reinterpret_cast<uint8_t*>(&out.success), 1u))
			return std::nullopt;
		if (out.success != 0 && !r.ReadU64(out.character_id))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCharacterCreateResponsePacket(uint8_t success, uint64_t characterId, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1u))
			return {};
		if (success != 0 && !w.WriteU64(characterId))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeCharacterCreateResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
