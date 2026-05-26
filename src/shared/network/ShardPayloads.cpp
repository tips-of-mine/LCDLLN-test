#include "src/shared/network/ShardPayloads.h"
#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"

#include <vector>

namespace engine::network
{
	std::optional<ShardRegisterPayload> ParseShardRegisterPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ShardRegisterPayload out;
		if (!r.ReadString(out.name) || !r.ReadString(out.endpoint) || !r.ReadString(out.udp_endpoint))
			return std::nullopt;
		if (!r.ReadU32(out.max_capacity) || !r.ReadU32(out.current_load))
			return std::nullopt;
		if (!r.ReadString(out.build_version))
			return std::nullopt;
		// Présentation publique (M-server-meta). display_name + 2 octets enum.
		if (!r.ReadString(out.display_name))
			return std::nullopt;
		uint8_t modeByte = 0;
		uint8_t rulesetByte = 0;
		if (!r.ReadBytes(&modeByte, 1) || !r.ReadBytes(&rulesetByte, 1))
			return std::nullopt;
		out.game_mode = ClampGameMode(modeByte);
		out.ruleset = ClampRuleset(rulesetByte);
		if (!r.ReadString(out.region))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildShardRegisterPayload(std::string_view name, std::string_view endpoint,
		std::string_view udp_endpoint, uint32_t max_capacity, uint32_t current_load, std::string_view build_version,
		std::string_view display_name, ShardGameMode game_mode, ShardRuleset ruleset, std::string_view region)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(name) || !w.WriteString(endpoint) || !w.WriteString(udp_endpoint) || !w.WriteU32(max_capacity) || !w.WriteU32(current_load) || !w.WriteString(build_version))
			return {};
		const uint8_t modeByte = static_cast<uint8_t>(game_mode);
		const uint8_t rulesetByte = static_cast<uint8_t>(ruleset);
		if (!w.WriteString(display_name) || !w.WriteBytes(&modeByte, 1) || !w.WriteBytes(&rulesetByte, 1))
			return {};
		if (!w.WriteString(region))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<ShardRegisterOkPayload> ParseShardRegisterOkPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 4u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ShardRegisterOkPayload out;
		if (!r.ReadU32(out.shard_id))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildShardRegisterOkPacket(uint32_t shard_id, uint32_t requestId)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU32(shard_id))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeShardRegisterOk, 0, requestId, 0, payloadBytes))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildShardRegisterErrorPacket(ShardRegisterErrorCode code, uint32_t requestId)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU32(static_cast<uint32_t>(code)))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeShardRegisterError, 0, requestId, 0, payloadBytes))
			return {};
		return builder.Data();
	}

	std::optional<ShardHeartbeatPayload> ParseShardHeartbeatPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 16u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ShardHeartbeatPayload out;
		if (!r.ReadU32(out.shard_id) || !r.ReadU32(out.current_load) || !r.ReadU64(out.timestamp))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildShardHeartbeatPayload(uint32_t shard_id, uint32_t current_load, uint64_t timestamp)
	{
		std::vector<uint8_t> buf(16u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(shard_id) || !w.WriteU32(current_load) || !w.WriteU64(timestamp))
			return {};
		return buf;
	}

	std::optional<AdmitCharacterPayload> ParseAdmitCharacterPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 16u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		AdmitCharacterPayload out;
		if (!r.ReadU64(out.account_id) || !r.ReadU64(out.character_id))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAdmitCharacterPacket(uint64_t account_id, uint64_t character_id)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(account_id) || !w.WriteU64(character_id))
			return {};
		const size_t payloadBytes = w.Offset();
		// Push master→shard, pas de request_id ni session.
		if (!builder.Finalize(kOpcodeMasterToShardAdmitCharacter, 0, 0, 0, payloadBytes))
			return {};
		return builder.Data();
	}
}
