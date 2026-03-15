#pragma once

#include "engine/server/SessionManager.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Maps connection id to session id for heartbeat liveness (M20.6). Thread-safe.
	/// Register after auth success; watchdog uses CollectExpired to close stale connections.
	class ConnectionSessionMap
	{
	public:
		ConnectionSessionMap() = default;

		/// Register \a connId as having \a sessionId (call after auth success).
		void Add(uint32_t connId, uint64_t sessionId);

		/// Remove \a connId from the map (call after closing connection).
		void Remove(uint32_t connId);

		/// Returns session_id for \a connId if registered. For M22.4 (shard ticket: connId → account_id).
		std::optional<uint64_t> GetSessionId(uint32_t connId) const;

		/// Returns pairs (connId, session_id) for which the session is no longer valid (heartbeat timeout or max age).
		/// Call after SessionManager::EvictExpired(). Caller must then close each connId and SessionManager::Close(session_id).
		std::vector<std::pair<uint32_t, uint64_t>> CollectExpired(const SessionManager& sessionManager);

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<uint32_t, uint64_t> m_connToSession;
	};

}
