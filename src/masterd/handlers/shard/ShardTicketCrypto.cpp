#include "src/masterd/handlers/shard/ShardTicketCrypto.h"
#include "src/shared/network/ByteWriter.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <array>
#include <cstring>

namespace engine::server
{
	namespace
	{
		constexpr size_t kMessageSize = engine::network::kShardTicketIdSize + 8u + 4u + 8u + 8u; // 44 (TA.3 : +character_id)
	}

	bool ComputeTicketHmac(const uint8_t* ticket_id, size_t ticket_id_size,
		uint64_t account_id, uint32_t target_shard_id, uint64_t expires_at, uint64_t character_id,
		std::string_view secret, uint8_t* outHmac, size_t outHmacSize)
	{
		using namespace engine::network;
		if (ticket_id == nullptr || ticket_id_size != kShardTicketIdSize || outHmac == nullptr || outHmacSize < kShardTicketHmacSize)
			return false;
		std::array<uint8_t, kMessageSize> msg{};
		engine::network::ByteWriter w(msg.data(), msg.size());
		if (!w.WriteBytes(ticket_id, ticket_id_size) || !w.WriteU64(account_id) || !w.WriteU32(target_shard_id) || !w.WriteU64(expires_at) || !w.WriteU64(character_id))
			return false;
		unsigned int len = 0;
		unsigned char* result = HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()), msg.data(), msg.size(), outHmac, &len);
		return result != nullptr && len == kShardTicketHmacSize;
	}

	bool VerifyTicketHmac(const uint8_t* ticket_id, size_t ticket_id_size,
		uint64_t account_id, uint32_t target_shard_id, uint64_t expires_at, uint64_t character_id,
		const uint8_t* hmac, size_t hmac_size, std::string_view secret)
	{
		using namespace engine::network;
		if (hmac == nullptr || hmac_size != kShardTicketHmacSize)
			return false;
		std::array<uint8_t, kShardTicketHmacSize> computed{};
		if (!ComputeTicketHmac(ticket_id, ticket_id_size, account_id, target_shard_id, expires_at, character_id, secret, computed.data(), computed.size()))
			return false;
		return std::memcmp(computed.data(), hmac, kShardTicketHmacSize) == 0;
	}
}
