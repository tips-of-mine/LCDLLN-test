#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

#include <openssl/rand.h>

namespace engine::server
{
	namespace
	{
		constexpr uint64_t kInvalidSessionId = 0;
	}

	void SessionManager::SetOnSessionClosed(std::function<void(uint64_t, uint64_t, SessionCloseReason)> hook)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_onSessionClosed = std::move(hook);
	}

	void SessionManager::SetSessionInWorldHook(std::function<bool(uint64_t)> hook)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sessionInWorldHook = std::move(hook);
	}

	void SessionManager::SetConfig(const SessionManagerConfig& config)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = config;
		LOG_DEBUG(Net, "[SessionManager] Config set: max_age_sec={} heartbeat_timeout_sec={} reconnection_window_sec={}",
			m_config.max_session_age_sec, m_config.heartbeat_timeout_sec, m_config.reconnection_window_sec);
	}

	SessionManagerConfig SessionManager::LoadConfig(const engine::core::Config& config)
	{
		SessionManagerConfig c;
		c.max_session_age_sec = config.GetInt("session.max_age_sec", 86400);
		c.heartbeat_timeout_sec = config.GetInt("session.heartbeat_timeout_sec", 120);
		c.reconnection_window_sec = config.GetInt("session.reconnection_window_sec", 300);
		std::string policy = config.GetString("session.duplicate_login_policy", "kick");
		if (policy == "refuse")
			c.duplicate_login_policy = DuplicateLoginPolicy::RefuseNew;
		else
			c.duplicate_login_policy = DuplicateLoginPolicy::KickExisting;
		return c;
	}

	bool SessionManager::isValid(const Session& s, Clock::time_point now) const
	{
		// Caller doit detenir m_mutex.
		if (s.state != SessionState::Authenticated && s.state != SessionState::Active)
			return false;
		if (now > s.expires_at)
			return false;
		auto idle_sec = std::chrono::duration_cast<std::chrono::seconds>(now - s.last_seen).count();
		if (idle_sec > m_config.heartbeat_timeout_sec)
			return false;
		return true;
	}

	uint64_t SessionManager::generateSessionId()
	{
		// Audit 2026-05-18 : avant ce fix, on utilisait std::mt19937_64 (non-CSPRNG)
		// seede par std::random_device. Mersenne Twister n'est PAS un CSPRNG : apres
		// ~624 sorties consecutives observees, l'etat interne est recuperable et
		// tous les session_id futurs deviennent predictibles. On bascule sur
		// OpenSSL RAND_bytes (meme pattern que ShardTicketHandler.cpp:108).
		unsigned char buf[sizeof(uint64_t)];
		if (RAND_bytes(buf, static_cast<int>(sizeof(buf))) != 1)
		{
			LOG_ERROR(Net, "[SessionManager] RAND_bytes failed during generateSessionId");
			return kInvalidSessionId;
		}
		uint64_t id = 0;
		std::memcpy(&id, buf, sizeof(uint64_t));
		// Eviter la sentinelle kInvalidSessionId (0). Probabilite 1/2^64 -> negligeable
		// mais on garde le check pour eliminer le doute.
		if (id == kInvalidSessionId)
			id = 1;
		return id;
	}

	void SessionManager::closeInternal_NoLock(uint64_t session_id, SessionCloseReason reason)
	{
		// Caller doit detenir m_mutex.
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return;
		uint64_t account_id = it->second.account_id;
		it->second.state = SessionState::Closed;
		m_by_account_id.erase(account_id);
		LOG_INFO(Net, "[SessionManager] Close: session_id={} account_id={} reason={}", session_id, account_id, SessionCloseReasonToString(reason));
		// Fire le hook APRES log + cleanup interne. La copie locale du hook evite
		// un crash si le subscriber le reset au milieu de l'appel.
		// IMPORTANT : on appelle le hook SANS detenir m_mutex pour eviter une
		// reentrance vers SessionManager via le subscriber (ex. fermeture TCP
		// qui pourrait redeclencher une logique session).
		auto hook = m_onSessionClosed;
		if (hook)
		{
			// Le caller doit avoir un std::unique_lock plutot qu'un lock_guard si
			// le hook a besoin d'etre appele sans le verrou. Convention ici : on
			// fire le hook SOUS verrou (simpler) ; les subscribers doivent eviter
			// de re-entrer dans SessionManager. Si necessite future de fire hors
			// verrou, refactor caller en unique_lock + unlock avant hook.
			hook(session_id, account_id, reason);
		}
	}

	uint64_t SessionManager::CreateSession(uint64_t account_id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto now = Clock::now();
		auto expires_at = now + std::chrono::seconds(m_config.max_session_age_sec);

		auto it = m_by_account_id.find(account_id);
		if (it != m_by_account_id.end())
		{
			uint64_t existing_id = it->second;
			auto sit = m_by_session_id.find(existing_id);
			if (sit != m_by_session_id.end() && isValid(sit->second, now))
			{
				// Protection prioritaire : si la session existante correspond à un joueur
				// réellement en jeu (EnterWorld validé), on refuse la nouvelle auth quelle
				// que soit la policy — le joueur en place n'est jamais kické. KickExisting
				// ne s'applique donc qu'aux sessions pré-monde transitoires (typiquement le
				// double-auth du flux de connexion client : auth initiale + re-auth du flow).
				if (m_sessionInWorldHook && m_sessionInWorldHook(existing_id))
				{
					LOG_INFO(Net, "[SessionManager] CreateSession refused: account_id={} already in-world (session_id={})", account_id, existing_id);
					return kInvalidSessionId;
				}
				if (m_config.duplicate_login_policy == DuplicateLoginPolicy::RefuseNew)
				{
					LOG_INFO(Net, "[SessionManager] CreateSession refused: account_id={} already has active session", account_id);
					return kInvalidSessionId;
				}
				// IMPORTANT : on detient deja m_mutex, donc on doit appeler la variante
				// sans verrou pour eviter un deadlock.
				closeInternal_NoLock(existing_id, SessionCloseReason::KickedByDuplicateLogin);
			}
		}

		uint64_t session_id = generateSessionId();
		if (session_id == kInvalidSessionId)
			return kInvalidSessionId;

		while (m_by_session_id.count(session_id))
		{
			session_id = generateSessionId();
			if (session_id == kInvalidSessionId)
				return kInvalidSessionId;
		}

		Session s;
		s.account_id = account_id;
		s.session_id = session_id;
		s.created_at = now;
		s.last_seen = now;
		s.expires_at = expires_at;
		s.state = SessionState::Created;

		m_by_session_id[session_id] = s;
		m_by_account_id[account_id] = session_id;

		LOG_INFO(Net, "[SessionManager] CreateSession OK: account_id={} session_id={}", account_id, session_id);
		return session_id;
	}

	bool SessionManager::Validate(uint64_t session_id) const
	{
		if (session_id == kInvalidSessionId)
			return false;
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return false;
		auto now = Clock::now();
		return isValid(it->second, now);
	}

	bool SessionManager::Touch(uint64_t session_id)
	{
		if (session_id == kInvalidSessionId)
			return false;
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return false;
		auto now = Clock::now();
		if (!isValid(it->second, now))
			return false;
		it->second.last_seen = now;
		LOG_DEBUG(Net, "[SessionManager] Touch OK: session_id={}", session_id);
		return true;
	}

	void SessionManager::Close(uint64_t session_id, SessionCloseReason reason)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		closeInternal_NoLock(session_id, reason);
	}

	void SessionManager::SetState(uint64_t session_id, SessionState state)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return;
		auto now = Clock::now();
		if (it->second.state != SessionState::Created && it->second.state != SessionState::Authenticated && it->second.state != SessionState::Active)
			return;
		if (now > it->second.expires_at)
			return;
		it->second.state = state;
		LOG_DEBUG(Net, "[SessionManager] SetState: session_id={} state={}", session_id, static_cast<int>(state));
	}

	size_t SessionManager::GetActiveCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		size_t n = 0;
		for (const auto& [id, s] : m_by_session_id)
		{
			(void)id;
			if (s.state == SessionState::Authenticated || s.state == SessionState::Active)
				++n;
		}
		return n;
	}

	std::optional<uint64_t> SessionManager::GetAccountId(uint64_t session_id) const
	{
		if (session_id == kInvalidSessionId)
			return std::nullopt;
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_by_session_id.find(session_id);
		if (it == m_by_session_id.end())
			return std::nullopt;
		return it->second.account_id;
	}

	/// Enumere tous les accountId associes a une session en etat
	/// Authenticated ou Active. Utilise par AdminCommandHandler::DispatchWho
	/// pour construire la liste des joueurs connectes. Pas de garantie
	/// d'ordre (unordered_map). Pas de filtrage role : tous les comptes
	/// connectes remontent.
	std::vector<uint64_t> SessionManager::ListActiveAccountIds() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<uint64_t> out;
		out.reserve(m_by_session_id.size());
		for (const auto& [id, s] : m_by_session_id)
		{
			(void)id;
			if (s.state == SessionState::Authenticated || s.state == SessionState::Active)
				out.push_back(s.account_id);
		}
		return out;
	}

	void SessionManager::EvictExpired()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto now = Clock::now();
		for (auto it = m_by_session_id.begin(); it != m_by_session_id.end(); )
		{
			Session& s = it->second;
			if (s.state == SessionState::Closed || s.state == SessionState::Expired)
			{
				++it;
				continue;
			}
			bool past_max = now > s.expires_at;
			auto idle_sec = std::chrono::duration_cast<std::chrono::seconds>(now - s.last_seen).count();
			bool heartbeat_expired = idle_sec > m_config.heartbeat_timeout_sec;
			if (past_max || heartbeat_expired)
			{
				s.state = SessionState::Expired;
				m_by_account_id.erase(s.account_id);
				LOG_INFO(Net, "[SessionManager] EvictExpired: session_id={} (past_max={} heartbeat_expired={})", s.session_id, past_max, heartbeat_expired);
				++it;
			}
			else
				++it;
		}
	}

	std::string SessionCloseReasonToString(SessionCloseReason reason)
	{
		switch (reason)
		{
		case SessionCloseReason::Logout: return "Logout";
		case SessionCloseReason::KickedByDuplicateLogin: return "KickedByDuplicateLogin";
		case SessionCloseReason::HeartbeatTimeout: return "HeartbeatTimeout";
		case SessionCloseReason::MaxAgeExceeded: return "MaxAgeExceeded";
		case SessionCloseReason::Admin: return "Admin";
		case SessionCloseReason::Other: return "Other";
		default: return "Unknown";
		}
	}
}
