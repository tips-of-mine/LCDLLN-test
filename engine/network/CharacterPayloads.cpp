#include "engine/network/CharacterPayloads.h"

#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"

namespace engine::network
{
	// -------------------------------------------------------------------------
	// ParseCharacterCreateRequestPayload
	// M39.1: reads name (string) + raceId (string) + classId (string)
	//        + 5 customization bytes.
	// Falls back gracefully when the extra fields are absent (legacy format).
	// -------------------------------------------------------------------------
	std::optional<CharacterCreateRequestPayload> ParseCharacterCreateRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload)
			return std::nullopt;

		ByteReader r(payload, payloadSize);
		CharacterCreateRequestPayload out;

		if (!r.ReadString(out.name))
			return std::nullopt;

		// M39.1 extended fields — optional (backward-compat if server sends legacy).
		if (r.Remaining() > 0)
		{
			if (!r.ReadString(out.raceId))
				return std::nullopt;
		}
		if (r.Remaining() > 0)
		{
			if (!r.ReadString(out.classId))
				return std::nullopt;
		}
		// Customization: 5 bytes, all optional.
		if (r.Remaining() >= 1u)
		{
			uint8_t b = 0;
			if (r.ReadBytes(&b, 1u)) out.customization.faceType = b;
		}
		if (r.Remaining() >= 1u)
		{
			uint8_t b = 0;
			if (r.ReadBytes(&b, 1u)) out.customization.hairStyle = b;
		}
		if (r.Remaining() >= 1u)
		{
			uint8_t b = 0;
			if (r.ReadBytes(&b, 1u)) out.customization.skinColorIdx = b;
		}
		if (r.Remaining() >= 1u)
		{
			uint8_t b = 0;
			if (r.ReadBytes(&b, 1u)) out.customization.hairColorIdx = b;
		}
		if (r.Remaining() >= 1u)
		{
			uint8_t b = 0;
			if (r.ReadBytes(&b, 1u)) out.customization.eyeColorIdx = b;
		}

		return out;
	}

	// -------------------------------------------------------------------------
	// BuildCharacterCreateRequestPayload
	// M39.1: writes name + raceId + classId + 5 customization bytes.
	// -------------------------------------------------------------------------
	std::vector<uint8_t> BuildCharacterCreateRequestPayload(std::string_view name,
	                                                        std::string_view raceId,
	                                                        std::string_view classId,
	                                                        const CharacterCustomization& customization)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());

		if (!w.WriteString(name))
			return {};

		// M39.1 extended fields.
		if (!w.WriteString(raceId))
			return {};
		if (!w.WriteString(classId))
			return {};

		// Customization (5 bytes).
		const uint8_t customBytes[5] = {
			customization.faceType,
			customization.hairStyle,
			customization.skinColorIdx,
			customization.hairColorIdx,
			customization.eyeColorIdx
		};
		if (!w.WriteBytes(customBytes, 5u))
			return {};

		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// ParseCharacterCreateResponsePayload — unchanged from M33.1.
	// -------------------------------------------------------------------------
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

	// -------------------------------------------------------------------------
	// BuildCharacterCreateResponsePacket — unchanged.
	// -------------------------------------------------------------------------
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
