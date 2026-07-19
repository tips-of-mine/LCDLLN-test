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
		// v9 : tableau optionnel de joueurs en queue. Absent pour un heartbeat legacy
		// (Remaining() == 0). Si présent, on lit u16 playerCount puis chaque entrée.
		if (r.Remaining() > 0u)
		{
			uint16_t playerCount = 0;
			if (!r.ReadU16(playerCount))
				return std::nullopt;
			out.players.reserve(playerCount);
			for (uint16_t i = 0; i < playerCount; ++i)
			{
				ShardPlayerPresence p;
				if (!r.ReadU64(p.accountId) || !r.ReadU64(p.characterId)
					|| !r.ReadU32(p.level) || !r.ReadU32(p.zoneId))
					return std::nullopt;
				out.players.push_back(p);
			}
		}
		return out;
	}

	std::vector<uint8_t> BuildShardHeartbeatPayload(uint32_t shard_id, uint32_t current_load, uint64_t timestamp,
		const std::vector<ShardPlayerPresence>& players)
	{
		// 16 octets fixes + (si joueurs) u16 count + 24 octets/joueur.
		const size_t total = players.empty() ? 16u : (16u + 2u + players.size() * 24u);
		std::vector<uint8_t> buf(total, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(shard_id) || !w.WriteU32(current_load) || !w.WriteU64(timestamp))
			return {};
		if (!players.empty())
		{
			if (!w.WriteU16(static_cast<uint16_t>(players.size())))
				return {};
			for (const auto& p : players)
			{
				if (!w.WriteU64(p.accountId) || !w.WriteU64(p.characterId)
					|| !w.WriteU32(p.level) || !w.WriteU32(p.zoneId))
					return {};
			}
		}
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
		// TD.5 — character_name optionnel : un master ancien ne l'enverra pas et le payload
		// s'arrête à 16 octets. Dans ce cas, on garde out.character_name vide et le shard
		// retombera sur le fallback "P<clientId>" (comportement legacy).
		if (r.Remaining() > 0u)
		{
			if (!r.ReadString(out.character_name))
				return std::nullopt;
		}
		// TD.6 — gender optionnel : un master sans la migration 0067 ne l'enverra pas. Dans
		// ce cas, le client retombera sur "male" par défaut côté rendu.
		if (r.Remaining() > 0u)
		{
			if (!r.ReadString(out.gender))
				return std::nullopt;
		}
		// Roadmap-7 — guild_id optionnel EN QUEUE (après gender, ne jamais insérer
		// avant les champs optionnels existants) : un master ancien ne l'émet pas
		// → 0 (sans guilde), repli propre côté shard (pas de partage guilde).
		if (r.Remaining() > 0u)
		{
			if (!r.ReadU64(out.guild_id))
				return std::nullopt;
		}
		return out;
	}

	std::vector<uint8_t> BuildAdmitCharacterPacket(uint64_t account_id, uint64_t character_id,
		std::string_view character_name, std::string_view gender, uint64_t guild_id)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(account_id) || !w.WriteU64(character_id))
			return {};
		if (!w.WriteString(character_name))
			return {};
		if (!w.WriteString(gender))
			return {};
		// Roadmap-7 — guilde du compte, champ additif en queue (cf. Parse).
		if (!w.WriteU64(guild_id))
			return {};
		const size_t payloadBytes = w.Offset();
		// Push master→shard, pas de request_id ni session.
		if (!builder.Finalize(kOpcodeMasterToShardAdmitCharacter, 0, 0, 0, payloadBytes))
			return {};
		return builder.Data();
	}
}
