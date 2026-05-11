#pragma once

#include "src/masterd/session/SessionManager.h"

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

		/// Reverse lookup : connId associe a \a sessionId si enregistre. Utilise par le
		/// hook OnSessionClosed (cf. SessionManager) pour fermer la TCP de la session
		/// duplicate-login. Walk O(N) sous mutex ; N petit (connexions actives) donc
		/// pas de besoin de map inverse maintenue en permanence.
		std::optional<uint32_t> FindConnIdForSession(uint64_t sessionId) const;

		/// Chat MVP — Snapshot des paires (connId, sessionId) actuellement enregistrées.
		/// Utilisé par les handlers broadcast (chat) qui doivent envoyer un même paquet à
		/// toutes les sessions actives. Verrouille brièvement le mutex pour copier la map ;
		/// le snapshot peut être obsolète au moment où l'appelant l'utilise (race normale).
		std::vector<std::pair<uint32_t, uint64_t>> Snapshot() const;

		/// Returns pairs (connId, session_id) for which the session is no longer valid (heartbeat timeout or max age).
		/// Call after SessionManager::EvictExpired(). Caller must then close each connId and SessionManager::Close(session_id).
		std::vector<std::pair<uint32_t, uint64_t>> CollectExpired(const SessionManager& sessionManager);

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<uint32_t, uint64_t> m_connToSession;
	};

}
