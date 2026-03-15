#pragma once

#include "engine/network/ShardTicketPayloads.h"

#include <cstdint>
#include <string_view>

namespace engine::server
{
	/// HMAC-SHA256 for shard ticket (v1). Master and Shard share the same secret.

	/// Computes HMAC over (ticket_id || account_id || target_shard_id || expires_at).
	/// \a outHmac must have at least kShardTicketHmacSize bytes. Returns true on success.
	bool ComputeTicketHmac(const uint8_t* ticket_id, size_t ticket_id_size,
		uint64_t account_id, uint32_t target_shard_id, uint64_t expires_at,
		std::string_view secret, uint8_t* outHmac, size_t outHmacSize);

	/// Verifies HMAC of the given ticket fields. Returns true if \a hmac matches.
	bool VerifyTicketHmac(const uint8_t* ticket_id, size_t ticket_id_size,
		uint64_t account_id, uint32_t target_shard_id, uint64_t expires_at,
		const uint8_t* hmac, size_t hmac_size, std::string_view secret);
}
