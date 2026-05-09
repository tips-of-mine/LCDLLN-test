#include "engine/server/ShardTicketValidator.h"
#include "engine/server/ShardTicketCrypto.h"
#include "engine/network/ShardTicketPayloads.h"
#include "engine/core/Log.h"

#include <chrono>
#include <sstream>
#include <iomanip>

namespace engine::server
{
	namespace
	{
		uint64_t NowSecondsSinceEpoch()
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}
	}

	std::string ShardTicketValidator::TicketIdToKey(const engine::network::ShardTicketId& id)
	{
		std::ostringstream os;
		os << std::hex << std::setfill('0');
		for (size_t i = 0; i < id.size(); ++i)
			os << std::setw(2) << static_cast<unsigned>(id[i]);
		return os.str();
	}

	std::optional<ShardTicketAccept> ShardTicketValidator::VerifyAndConsume(const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (payload == nullptr)
			return std::nullopt;
		auto data = ParseShardTicketPayload(payload, payloadSize);
		if (!data)
		{
			LOG_DEBUG(Core, "[ShardTicketValidator] VerifyAndConsume: parse failed");
			return std::nullopt;
		}
		if (m_secret.empty())
		{
			LOG_WARN(Core, "[ShardTicketValidator] VerifyAndConsume: no secret set");
			return std::nullopt;
		}
		uint64_t now = NowSecondsSinceEpoch();
		if (data->expires_at <= now)
		{
			LOG_DEBUG(Core, "[ShardTicketValidator] VerifyAndConsume: ticket expired (expires_at={} now={})", data->expires_at, now);
			return std::nullopt;
		}
		if (!VerifyTicketHmac(data->ticket_id.data(), data->ticket_id.size(), data->account_id, data->target_shard_id, data->expires_at,
			data->hmac.data(), data->hmac.size(), m_secret))
		{
			LOG_WARN(Core, "[ShardTicketValidator] VerifyAndConsume: invalid HMAC (account_id={} target_shard_id={})", data->account_id, data->target_shard_id);
			return std::nullopt;
		}
		if (m_shard_id != 0 && data->target_shard_id != m_shard_id)
		{
			LOG_DEBUG(Core, "[ShardTicketValidator] VerifyAndConsume: wrong shard (ticket target={} validator shard={})", data->target_shard_id, m_shard_id);
			return std::nullopt;
		}
		std::string key = TicketIdToKey(data->ticket_id);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_used_ticket_ids.count(key) != 0)
			{
				LOG_WARN(Core, "[ShardTicketValidator] VerifyAndConsume: ticket already used (account_id={})", data->account_id);
				return std::nullopt;
			}
			m_used_ticket_ids.insert(key);
		}
		ShardTicketAccept accept;
		accept.account_id = data->account_id;
		accept.target_shard_id = data->target_shard_id;
		LOG_INFO(Core, "[ShardTicketValidator] Ticket accepted (account_id={} target_shard_id={})", accept.account_id, accept.target_shard_id);
		return accept;
	}
}
