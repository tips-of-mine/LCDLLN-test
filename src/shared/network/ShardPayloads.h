#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// Parsed SHARD_REGISTER payload: name, endpoint, max_capacity, current_load, build_version.
	struct ShardRegisterPayload
	{
		std::string name;
		std::string endpoint;
		uint32_t max_capacity = 0;
		uint32_t current_load = 0;
		std::string build_version;
	};

	/// Parses SHARD_REGISTER payload. Returns nullopt if truncated or invalid.
	std::optional<ShardRegisterPayload> ParseShardRegisterPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_REGISTER payload (Shard→Master).
	std::vector<uint8_t> BuildShardRegisterPayload(std::string_view name, std::string_view endpoint,
		uint32_t max_capacity, uint32_t current_load, std::string_view build_version);

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
