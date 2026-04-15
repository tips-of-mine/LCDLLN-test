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
		// M39.1 — extended fields: read if present; use defaults when absent (backwards compat).
		(void)r.ReadU32(out.raceId);
		(void)r.ReadU32(out.classId);
		uint8_t tmp = 0;
		if (r.ReadBytes(&tmp, 1u)) out.faceType       = tmp;
		if (r.ReadBytes(&tmp, 1u)) out.hairStyle      = tmp;
		if (r.ReadBytes(&tmp, 1u)) out.skinColorIndex = tmp;
		if (r.ReadBytes(&tmp, 1u)) out.hairColorIndex = tmp;
		if (r.ReadBytes(&tmp, 1u)) out.eyeColorIndex  = tmp;
		return out;
	}

	std::vector<uint8_t> BuildCharacterCreateRequestPayload(std::string_view name)
	{
		CharacterCreateRequestPayload p{};
		p.name = std::string(name);
		return BuildCharacterCreateRequestPayload(p);
	}

	std::vector<uint8_t> BuildCharacterCreateRequestPayload(const CharacterCreateRequestPayload& payload)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(payload.name))
			return {};
		// M39.1 — extended fields.
		if (!w.WriteU32(payload.raceId))  return {};
		if (!w.WriteU32(payload.classId)) return {};
		const uint8_t custom[5] = {
		    payload.faceType, payload.hairStyle,
		    payload.skinColorIndex, payload.hairColorIndex, payload.eyeColorIndex
		};
		if (!w.WriteBytes(custom, sizeof(custom))) return {};
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
