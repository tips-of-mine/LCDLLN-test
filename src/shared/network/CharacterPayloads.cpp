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

	// -------------------------------------------------------------------------
	// Phase 1 — CHARACTER_LIST request/response payloads.
	// Request : uint32 serverId
	// Response: uint8 success [+ uint16 count + entries...]
	// Entry   : uint64 id | uint8 slot | string name | uint32 raceId | uint16 classId
	//           | uint16 level | uint8 forceRename | uint64 lastSeenUnix | uint64 totalPlaySecs
	// -------------------------------------------------------------------------
	std::optional<CharacterListRequestPayload> ParseCharacterListRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterListRequestPayload out;
		if (!r.ReadU32(out.serverId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCharacterListRequestPayload(uint32_t serverId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(serverId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<CharacterListResponsePayload> ParseCharacterListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterListResponsePayload out;
		uint8_t success = 0;
		if (!r.ReadBytes(&success, 1u))
			return std::nullopt;
		out.success = success;
		if (success == 0)
			return out;
		uint16_t count = 0;
		if (!r.ReadU16(count))
			return std::nullopt;
		out.entries.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			CharacterListEntry e;
			uint8_t slotByte = 0;
			uint8_t forceRenameByte = 0;
			if (!r.ReadU64(e.character_id))
				return std::nullopt;
			if (!r.ReadBytes(&slotByte, 1u))
				return std::nullopt;
			e.slot = slotByte;
			if (!r.ReadString(e.name))
				return std::nullopt;
			if (!r.ReadU32(e.race_id) || !r.ReadU16(e.class_id) || !r.ReadU16(e.level))
				return std::nullopt;
			if (!r.ReadBytes(&forceRenameByte, 1u))
				return std::nullopt;
			e.force_rename = forceRenameByte;
			if (!r.ReadU64(e.last_seen_unix) || !r.ReadU64(e.total_play_secs))
				return std::nullopt;
			// Phase 3.6 — 5 floats spawn (20 octets). Wire format strictement LE
			// (match x86 / Vulkan). Pas tolérant aux entrées tronquées : si la DB
			// pré-migration renvoie 0 partout, le client appliquera les défauts engine.
			if (!r.ReadBytes(reinterpret_cast<uint8_t*>(&e.spawn_x), sizeof(float))
				|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&e.spawn_y), sizeof(float))
				|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&e.spawn_z), sizeof(float))
				|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&e.spawn_yaw_deg), sizeof(float))
				|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&e.spawn_pitch_deg), sizeof(float)))
			{
				return std::nullopt;
			}
			// Phase 3.8 — race/class strings (length-prefixed UTF-8).
			if (!r.ReadString(e.race_str) || !r.ReadString(e.class_str))
				return std::nullopt;
			out.entries.push_back(std::move(e));
		}
		return out;
	}

	std::vector<uint8_t> BuildCharacterListResponsePacket(uint8_t success, const std::vector<CharacterListEntry>& entries,
	                                                     uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1u))
			return {};
		if (success != 0)
		{
			if (!w.WriteU16(static_cast<uint16_t>(entries.size())))
				return {};
			for (const auto& e : entries)
			{
				const uint8_t slotByte = e.slot;
				const uint8_t forceRenameByte = e.force_rename;
				if (!w.WriteU64(e.character_id))
					return {};
				if (!w.WriteBytes(&slotByte, 1u))
					return {};
				if (!w.WriteString(e.name))
					return {};
				if (!w.WriteU32(e.race_id) || !w.WriteU16(e.class_id) || !w.WriteU16(e.level))
					return {};
				if (!w.WriteBytes(&forceRenameByte, 1u))
					return {};
				if (!w.WriteU64(e.last_seen_unix) || !w.WriteU64(e.total_play_secs))
					return {};
				// Phase 3.6 — 5 floats spawn (20 octets), strict LE.
				if (!w.WriteBytes(reinterpret_cast<const uint8_t*>(&e.spawn_x), sizeof(float))
					|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&e.spawn_y), sizeof(float))
					|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&e.spawn_z), sizeof(float))
					|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&e.spawn_yaw_deg), sizeof(float))
					|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&e.spawn_pitch_deg), sizeof(float)))
				{
					return {};
				}
				// Phase 3.8 — race / class strings.
				if (!w.WriteString(e.race_str) || !w.WriteString(e.class_str))
					return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeCharacterListResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// Phase 3.9 — CHARACTER_DELETE request/response payloads.
	// Request : uint64 character_id (8 octets).
	// Response: uint8 success.
	// -------------------------------------------------------------------------
	std::optional<CharacterDeleteRequestPayload> ParseCharacterDeleteRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterDeleteRequestPayload out;
		if (!r.ReadU64(out.characterId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCharacterDeleteRequestPayload(uint64_t characterId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(characterId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<CharacterDeleteResponsePayload> ParseCharacterDeleteResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterDeleteResponsePayload out;
		uint8_t success = 0;
		if (!r.ReadBytes(&success, 1u))
			return std::nullopt;
		out.success = success;
		return out;
	}

	std::vector<uint8_t> BuildCharacterDeleteResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1u))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeCharacterDeleteResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// Phase 3.6.5 — CHARACTER_SAVE_POSITION request/response.
	// Request : uint64 character_id + 5 × float (28 octets).
	// Response: uint8 success.
	// -------------------------------------------------------------------------
	std::optional<CharacterSavePositionRequestPayload> ParseCharacterSavePositionRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 28u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterSavePositionRequestPayload out;
		if (!r.ReadU64(out.characterId))
			return std::nullopt;
		if (!r.ReadBytes(reinterpret_cast<uint8_t*>(&out.x), sizeof(float))
			|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&out.y), sizeof(float))
			|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&out.z), sizeof(float))
			|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&out.yawDeg), sizeof(float))
			|| !r.ReadBytes(reinterpret_cast<uint8_t*>(&out.pitchDeg), sizeof(float)))
		{
			return std::nullopt;
		}
		return out;
	}

	std::vector<uint8_t> BuildCharacterSavePositionRequestPayload(uint64_t characterId, float x, float y, float z, float yawDeg, float pitchDeg)
	{
		std::vector<uint8_t> buf(28u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(characterId))
			return {};
		if (!w.WriteBytes(reinterpret_cast<const uint8_t*>(&x), sizeof(float))
			|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&y), sizeof(float))
			|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&z), sizeof(float))
			|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&yawDeg), sizeof(float))
			|| !w.WriteBytes(reinterpret_cast<const uint8_t*>(&pitchDeg), sizeof(float)))
		{
			return {};
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<CharacterSavePositionResponsePayload> ParseCharacterSavePositionResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterSavePositionResponsePayload out;
		uint8_t success = 0;
		if (!r.ReadBytes(&success, 1u))
			return std::nullopt;
		out.success = success;
		return out;
	}

	std::vector<uint8_t> BuildCharacterSavePositionResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1u))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeCharacterSavePositionResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// Phase 4 chat — CHARACTER_ENTER_WORLD request/response.
	// Request : uint64 character_id + string character_name (length-prefixed UTF-8).
	// Response: uint8 success.
	// -------------------------------------------------------------------------
	std::optional<CharacterEnterWorldRequestPayload> ParseCharacterEnterWorldRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterEnterWorldRequestPayload out;
		if (!r.ReadU64(out.characterId))
			return std::nullopt;
		if (!r.ReadString(out.characterName))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCharacterEnterWorldRequestPayload(uint64_t characterId, std::string_view characterName)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(characterId))
			return {};
		if (!w.WriteString(characterName))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<CharacterEnterWorldResponsePayload> ParseCharacterEnterWorldResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		CharacterEnterWorldResponsePayload out;
		uint8_t success = 0;
		if (!r.ReadBytes(&success, 1u))
			return std::nullopt;
		out.success = success;
		return out;
	}

	std::vector<uint8_t> BuildCharacterEnterWorldResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1u))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeCharacterEnterWorldResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
