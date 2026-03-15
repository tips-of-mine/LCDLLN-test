#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	/// Single server list entry (M22.5): shard_id, status, current_load, max_capacity, optional character_count. M22.6: endpoint for client→shard connect.
	struct ServerListEntry
	{
		uint32_t shard_id = 0;
		uint8_t status = 0;   ///< ShardState value (Registering=0, Online=1, Degraded=2, Offline=3).
		uint32_t current_load = 0;
		uint32_t max_capacity = 0;
		uint32_t character_count = 0; ///< Optional, from hook (M20/M21); 0 if not provided.
		std::string endpoint;          ///< M22.6: shard address (e.g. host:port) for CONNECT SHARD.
	};

	/// Parses SERVER_LIST_RESPONSE payload. Returns empty vector if invalid.
	std::vector<ServerListEntry> ParseServerListResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SERVER_LIST_RESPONSE payload.
	std::vector<uint8_t> BuildServerListResponsePayload(const std::vector<ServerListEntry>& entries);

	/// Builds full SERVER_LIST_RESPONSE packet (Master→Client).
	std::vector<uint8_t> BuildServerListResponsePacket(uint32_t requestId, const std::vector<uint8_t>& payload);
}
