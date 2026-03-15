#pragma once

#include "engine/network/ShardTicketPayloads.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

namespace engine::server
{
	/// Result of successful ticket validation (M22.4). Shard can open session for this account.
	struct ShardTicketAccept
	{
		uint64_t account_id = 0;
		uint32_t target_shard_id = 0;
	};

	/// Validates and consumes shard tickets (one-time use). Thread-safe.
	class ShardTicketValidator
	{
	public:
		ShardTicketValidator() = default;

		/// Secret for HMAC (must match Master). Empty = reject all.
		void SetSecret(std::string secret) { m_secret = std::move(secret); }
		/// If set, only accept tickets with target_shard_id == this value. Optional (0 = accept any).
		void SetShardId(uint32_t shard_id) { m_shard_id = shard_id; }

		/// Verifies signature and expiry, then consumes ticket (one-time use).
		/// Returns accept data on success; nullopt if expired, invalid HMAC, wrong shard, or already used.
		std::optional<ShardTicketAccept> VerifyAndConsume(const uint8_t* payload, size_t payloadSize);

	private:
		mutable std::mutex m_mutex;
		std::string m_secret;
		uint32_t m_shard_id = 0;
		std::unordered_set<std::string> m_used_ticket_ids;
		static std::string TicketIdToKey(const engine::network::ShardTicketId& id);
	};
}
