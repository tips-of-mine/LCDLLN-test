// ADM.0 — Relais des commandes MJ master → shard : voir GmShardRelay.h.

#include "src/masterd/admin/GmShardRelay.h"

#include "src/shared/core/Log.h"

namespace engine::server
{
	GmShardRelay::ForwardResult GmShardRelay::Forward(uint32_t shardId, uint32_t connId,
		engine::network::GmCommandRequestPayload request, double nowSeconds)
	{
		if (shardId == 0u)
			return ForwardResult::NoShard;
		std::vector<uint8_t> packet;
		uint64_t requestId = 0;
		{
			std::lock_guard lock(m_mutex);
			size_t inFlight = 0;
			for (const auto& [id, p] : m_pending)
			{
				if (p.accountId == request.actorAccountId)
					++inFlight;
			}
			if (inFlight >= kMaxPendingPerAccount)
				return ForwardResult::Busy;
			requestId = m_nextRequestId++;
			request.requestId = requestId;
			packet = engine::network::BuildMasterToShardGmCommandPacket(request);
			if (packet.empty())
				return ForwardResult::Invalid;
			m_pending[requestId] = Pending{ shardId, connId, request.actorAccountId, request.command, nowSeconds };
		}
		// Envoi hors verrou (le rappel peut écrire sur le réseau).
		if (!m_send || !m_send(shardId, packet))
		{
			std::lock_guard lock(m_mutex);
			m_pending.erase(requestId);
			return ForwardResult::SendFailed;
		}
		LOG_INFO(Net, "[GmShardRelay] ADM.0 commande /{} relayée (request_id={}, account={}, shard={})",
			request.command, requestId, request.actorAccountId, shardId);
		return ForwardResult::Sent;
	}

	bool GmShardRelay::OnResult(uint32_t fromShardId, const engine::network::GmCommandResultPayload& result)
	{
		Pending pending;
		{
			std::lock_guard lock(m_mutex);
			const auto it = m_pending.find(result.requestId);
			if (it == m_pending.end())
			{
				LOG_WARN(Net, "[GmShardRelay] ADM.0 résultat sans demande (request_id={}, shard={}) : ignoré",
					result.requestId, fromShardId);
				return false;
			}
			if (it->second.shardId != fromShardId || it->second.accountId != result.actorAccountId)
			{
				LOG_WARN(Net, "[GmShardRelay] ADM.0 résultat incohérent (request_id={}, attendu shard={} account={}, reçu shard={} account={}) : ignoré",
					result.requestId, it->second.shardId, it->second.accountId, fromShardId, result.actorAccountId);
				return false;
			}
			pending = it->second;
			m_pending.erase(it);
		}
		if (m_reply)
		{
			for (const std::string& line : result.lines)
				m_reply(pending.accountId, pending.connId, line);
		}
		LOG_INFO(Net, "[GmShardRelay] ADM.0 résultat /{} (request_id={}, account={}, status={}, lines={})",
			pending.command, result.requestId, pending.accountId, static_cast<unsigned>(result.status), result.lines.size());
		return true;
	}

	size_t GmShardRelay::Tick(double nowSeconds)
	{
		std::vector<Pending> expired;
		{
			std::lock_guard lock(m_mutex);
			for (auto it = m_pending.begin(); it != m_pending.end();)
			{
				if (nowSeconds - it->second.sentAtSeconds >= m_timeoutSeconds)
				{
					expired.push_back(it->second);
					it = m_pending.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		for (const Pending& p : expired)
		{
			if (m_reply)
				m_reply(p.accountId, p.connId, "Le shard n'a pas répondu à /" + p.command + " (délai dépassé).");
			LOG_WARN(Net, "[GmShardRelay] ADM.0 délai dépassé pour /{} (account={}, shard={})", p.command, p.accountId, p.shardId);
		}
		return expired.size();
	}

	size_t GmShardRelay::PendingCount() const
	{
		std::lock_guard lock(m_mutex);
		return m_pending.size();
	}
}
