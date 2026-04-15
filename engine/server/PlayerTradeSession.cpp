#include "engine/server/PlayerTradeSession.h"
#include "engine/core/Log.h"

#include <algorithm>

namespace engine::server
{
	// -------------------------------------------------------------------------
	// TradeSession helpers
	// -------------------------------------------------------------------------

	TradeSlot* TradeSession::FindSlot(uint32_t clientId)
	{
		if (slotA.clientId == clientId)
			return &slotA;
		if (slotB.clientId == clientId)
			return &slotB;
		return nullptr;
	}

	const TradeSlot* TradeSession::FindSlot(uint32_t clientId) const
	{
		if (slotA.clientId == clientId)
			return &slotA;
		if (slotB.clientId == clientId)
			return &slotB;
		return nullptr;
	}

	TradeSlot* TradeSession::FindOpponentSlot(uint32_t clientId)
	{
		if (slotA.clientId == clientId)
			return &slotB;
		if (slotB.clientId == clientId)
			return &slotA;
		return nullptr;
	}

	const TradeSlot* TradeSession::FindOpponentSlot(uint32_t clientId) const
	{
		if (slotA.clientId == clientId)
			return &slotB;
		if (slotB.clientId == clientId)
			return &slotA;
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// PlayerTradeManager
	// -------------------------------------------------------------------------

	void PlayerTradeManager::Init()
	{
		m_pendingRequests.clear();
		m_sessions.clear();
		m_nextSessionId = 1;
		m_initialized = true;
		LOG_INFO(Net, "[PlayerTradeManager] Init OK");
	}

	void PlayerTradeManager::Shutdown()
	{
		if (!m_initialized)
			return;
		m_pendingRequests.clear();
		m_sessions.clear();
		m_initialized = false;
		LOG_INFO(Net, "[PlayerTradeManager] Shutdown");
	}

	bool PlayerTradeManager::AddPendingRequest(uint32_t initiatorClientId, uint32_t targetClientId)
	{
		for (const PendingRequest& req : m_pendingRequests)
		{
			if (req.initiatorClientId == initiatorClientId)
			{
				LOG_WARN(Net, "[PlayerTradeManager] AddPendingRequest rejected: initiator {} already has a pending request",
					initiatorClientId);
				return false;
			}
		}
		m_pendingRequests.push_back({ initiatorClientId, targetClientId });
		LOG_DEBUG(Net, "[PlayerTradeManager] Pending request added (initiator={}, target={})",
			initiatorClientId, targetClientId);
		return true;
	}

	uint32_t PlayerTradeManager::FindPendingRequestInitiator(uint32_t targetClientId) const
	{
		for (const PendingRequest& req : m_pendingRequests)
		{
			if (req.targetClientId == targetClientId)
				return req.initiatorClientId;
		}
		return 0;
	}

	void PlayerTradeManager::CancelPendingRequest(uint32_t clientId)
	{
		const auto it = std::remove_if(
			m_pendingRequests.begin(),
			m_pendingRequests.end(),
			[clientId](const PendingRequest& req) {
				return req.initiatorClientId == clientId || req.targetClientId == clientId;
			});
		if (it != m_pendingRequests.end())
		{
			LOG_DEBUG(Net, "[PlayerTradeManager] Pending request cancelled for client_id={}", clientId);
			m_pendingRequests.erase(it, m_pendingRequests.end());
		}
	}

	uint32_t PlayerTradeManager::CreateSession(
		uint32_t clientIdA, uint32_t charKeyA, std::string_view displayNameA,
		uint32_t clientIdB, uint32_t charKeyB, std::string_view displayNameB)
	{
		TradeSession session{};
		session.sessionId = m_nextSessionId++;
		session.slotA.clientId = clientIdA;
		session.slotA.characterKey = charKeyA;
		session.slotA.displayName = std::string(displayNameA);
		session.slotB.clientId = clientIdB;
		session.slotB.characterKey = charKeyB;
		session.slotB.displayName = std::string(displayNameB);
		session.phase = TradePhase::Active;

		const uint32_t sessionId = session.sessionId;
		m_sessions.push_back(std::move(session));
		LOG_INFO(Net, "[PlayerTradeManager] Session {} created (A={}, B={})",
			sessionId, clientIdA, clientIdB);
		return sessionId;
	}

	TradeSession* PlayerTradeManager::FindSession(uint32_t clientId)
	{
		for (TradeSession& s : m_sessions)
		{
			if (s.slotA.clientId == clientId || s.slotB.clientId == clientId)
				return &s;
		}
		return nullptr;
	}

	const TradeSession* PlayerTradeManager::FindSession(uint32_t clientId) const
	{
		for (const TradeSession& s : m_sessions)
		{
			if (s.slotA.clientId == clientId || s.slotB.clientId == clientId)
				return &s;
		}
		return nullptr;
	}

	void PlayerTradeManager::RemoveSession(uint32_t sessionId)
	{
		const auto it = std::remove_if(
			m_sessions.begin(),
			m_sessions.end(),
			[sessionId](const TradeSession& s) { return s.sessionId == sessionId; });
		if (it != m_sessions.end())
		{
			LOG_DEBUG(Net, "[PlayerTradeManager] Session {} removed", sessionId);
			m_sessions.erase(it, m_sessions.end());
		}
	}

	void PlayerTradeManager::CancelSessionsForClient(uint32_t clientId)
	{
		CancelPendingRequest(clientId);

		const auto it = std::remove_if(
			m_sessions.begin(),
			m_sessions.end(),
			[clientId](const TradeSession& s) {
				return s.slotA.clientId == clientId || s.slotB.clientId == clientId;
			});
		if (it != m_sessions.end())
		{
			LOG_INFO(Net, "[PlayerTradeManager] Sessions cancelled for disconnected client_id={}", clientId);
			m_sessions.erase(it, m_sessions.end());
		}
	}
}
