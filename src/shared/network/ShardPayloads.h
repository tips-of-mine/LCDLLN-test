#pragma once

#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ServerMeta.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// Parsed SHARD_REGISTER payload: name, endpoint, max_capacity, current_load,
	/// build_version, plus présentation publique (display_name + game_mode + ruleset).
	struct ShardRegisterPayload
	{
		std::string name;            ///< Identifiant technique unique (clé de dédup à la reconnexion).
		std::string endpoint;
		std::string udp_endpoint;    ///< TB.1: endpoint UDP gameplay du shard (host:port). Vide si non annoncé.
		uint32_t max_capacity = 0;
		uint32_t current_load = 0;
		std::string build_version;
		std::string display_name;    ///< Nom public affiché au joueur (ne PAS confondre avec name).
		ShardGameMode game_mode = ShardGameMode::PvE;
		ShardRuleset ruleset = ShardRuleset::Cooperative;
		std::string region;          ///< Région annoncée (texte libre, ex. « eu-west »), exposée par l'API /status.
	};

	/// Parses SHARD_REGISTER payload. Returns nullopt if truncated or invalid.
	std::optional<ShardRegisterPayload> ParseShardRegisterPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_REGISTER payload (Shard→Master). TB.1: \a udp_endpoint annonce l'endpoint
	/// UDP gameplay du shard (relayé au client via SERVER_LIST).
	std::vector<uint8_t> BuildShardRegisterPayload(std::string_view name, std::string_view endpoint,
		std::string_view udp_endpoint, uint32_t max_capacity, uint32_t current_load, std::string_view build_version,
		std::string_view display_name = {}, ShardGameMode game_mode = ShardGameMode::PvE,
		ShardRuleset ruleset = ShardRuleset::Cooperative, std::string_view region = {});

	/// Parsed SHARD_REGISTER_OK payload: shard_id.
	struct ShardRegisterOkPayload
	{
		uint32_t shard_id = 0;
	};
	std::optional<ShardRegisterOkPayload> ParseShardRegisterOkPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_REGISTER_OK packet (Master→Shard). requestId from register request.
	std::vector<uint8_t> BuildShardRegisterOkPacket(uint32_t shard_id, uint32_t requestId);

	/// Error code for SHARD_REGISTER_ERROR (internal, not NetErrorCode).
	enum class ShardRegisterErrorCode : uint32_t
	{
		DuplicateName = 1,
		InvalidPayload = 2,
	};

	/// Builds SHARD_REGISTER_ERROR packet (Master→Shard).
	std::vector<uint8_t> BuildShardRegisterErrorPacket(ShardRegisterErrorCode code, uint32_t requestId);

	/// Parsed SHARD_HEARTBEAT payload: shard_id, current_load, timestamp (M22.3).
	struct ShardHeartbeatPayload
	{
		uint32_t shard_id = 0;
		uint32_t current_load = 0;
		uint64_t timestamp = 0;
	};
	std::optional<ShardHeartbeatPayload> ParseShardHeartbeatPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_HEARTBEAT payload (Shard→Master). timestamp: e.g. seconds since epoch or monotonic.
	std::vector<uint8_t> BuildShardHeartbeatPayload(uint32_t shard_id, uint32_t current_load, uint64_t timestamp = 0);
}
