// M21.6 — Connection pool: configurable size, health check (ping), reconnect on Acquire. Thread-safe.

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

#include <chrono>
#include <thread>

namespace engine::server::db
{
	namespace
	{
		constexpr int kAcquireTimeoutMs = 5000;
		constexpr int kAcquireRetryMs = 50;
	}

	ConnectionPool::Guard::Guard(ConnectionPool* pool, MYSQL* mysql) : m_pool(pool), m_mysql(mysql) {}

	ConnectionPool::Guard::Guard(Guard&& other) noexcept : m_pool(other.m_pool), m_mysql(other.m_mysql)
	{
		other.m_pool = nullptr;
		other.m_mysql = nullptr;
	}

	ConnectionPool::Guard& ConnectionPool::Guard::operator=(Guard&& other) noexcept
	{
		if (this != &other)
		{
			if (m_pool && m_mysql)
				m_pool->Release(m_mysql);
			m_pool = other.m_pool;
			m_mysql = other.m_mysql;
			other.m_pool = nullptr;
			other.m_mysql = nullptr;
		}
		return *this;
	}

	ConnectionPool::Guard::~Guard()
	{
		if (m_pool && m_mysql)
			m_pool->Release(m_mysql);
		m_pool = nullptr;
		m_mysql = nullptr;
	}

	bool ConnectionPool::ConnectOne(MYSQL* mysql) const
	{
		// CLIENT_FOUND_ROWS : mysql_affected_rows() retourne le nombre de rangs MATCHES
		// (et non changes) sur les UPDATE. Sans ce flag, un UPDATE avec valeurs
		// identiques (ex: joueur immobile -> spawn_x/y/z inchanges) renvoie 0 et
		// les handlers logguent un faux warning "no row updated". Avec ce flag,
		// la semantique "0 -> row absente" devient fiable. Aucun callsite n'a
		// besoin de la semantique "changed only" : tous attendent matched.
		return mysql_real_connect(mysql, m_host.c_str(), m_user.c_str(),
			m_password.empty() ? nullptr : m_password.c_str(), m_database.c_str(),
			m_port, nullptr, CLIENT_FOUND_ROWS) != nullptr;
	}

	bool ConnectionPool::Init(const engine::core::Config& config)
	{
		std::string host = config.GetString("db.host", "");
		if (host.empty())
		{
			LOG_WARN(Core, "[ConnectionPool] Init skipped: db.host empty");
			return false;
		}

		m_host = host;
		m_port = static_cast<unsigned int>(config.GetInt("db.port", 3306));
		m_user = config.GetString("db.user", "");
		m_password = config.GetString("db.password", "");
		m_database = config.GetString("db.database", "lcdlln_master");
		size_t poolSize = static_cast<size_t>(config.GetInt("db.pool_size", 4));
		if (poolSize == 0)
			poolSize = 1;

		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_initialized)
		{
			LOG_WARN(Core, "[ConnectionPool] Init already done");
			return true;
		}

		m_connections.resize(poolSize);
		size_t ok = 0;
		for (size_t i = 0; i < poolSize; ++i)
		{
			MYSQL* mysql = mysql_init(nullptr);
			if (!mysql)
			{
				LOG_ERROR(Core, "[ConnectionPool] mysql_init failed for slot {}", i);
				continue;
			}
			bool reconnect = false;
			mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
			if (!ConnectOne(mysql))
			{
				LOG_ERROR(Core, "[ConnectionPool] Connect failed for slot {}: {}", i, mysql_error(mysql));
				mysql_close(mysql);
				continue;
			}
			m_connections[i].mysql = mysql;
			m_connections[i].in_use = false;
			++ok;
		}

		if (ok == 0)
		{
			LOG_ERROR(Core, "[ConnectionPool] Init failed: no connection established");
			return false;
		}
		m_initialized = true;
		LOG_INFO(Core, "[ConnectionPool] Init OK (pool_size={}, connected={})", poolSize, ok);
		return true;
	}

	void ConnectionPool::Shutdown()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_initialized)
			return;
		for (auto& e : m_connections)
		{
			if (e.mysql)
			{
				mysql_close(e.mysql);
				e.mysql = nullptr;
				e.in_use = false;
			}
		}
		m_initialized = false;
		LOG_INFO(Core, "[ConnectionPool] Shutdown complete");
	}

	bool ConnectionPool::IsInitialized() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_initialized && !m_connections.empty();
	}

	ConnectionPool::Guard ConnectionPool::Acquire()
	{
		// Audit 2026-05-18 : avant ce fix, `mysql_ping` (round-trip reseau,
		// plusieurs ms a plusieurs secondes en cas de timeout) etait appele SOUS
		// m_mutex. Tous les autres workers DB etaient bloques pendant le ping
		// d'une seule connexion -> sous charge avec un slot mort, tout le pool
		// se serialisait sur la latence MySQL. Fix : extraire le slot sous
		// verrou, faire le ping + reconnect HORS verrou, puis re-prendre le
		// verrou seulement pour publier le resultat (mysql handle remplace).
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kAcquireTimeoutMs);
		for (;;)
		{
			// Phase 1 (verrou) : reserver un slot libre.
			size_t slot_index = static_cast<size_t>(-1);
			MYSQL* mysql_handle = nullptr;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_initialized)
					return Guard();
				for (size_t i = 0; i < m_connections.size(); ++i)
				{
					auto& e = m_connections[i];
					if (!e.in_use && e.mysql)
					{
						e.in_use = true; // reservation immediate, meme si le ping va echouer
						mysql_handle = e.mysql;
						slot_index = i;
						break;
					}
				}
			}

			if (mysql_handle == nullptr)
			{
				// Pas de slot libre : sleep court puis retry.
				if (std::chrono::steady_clock::now() >= deadline)
				{
					LOG_ERROR(Core, "[ConnectionPool] Acquire timeout");
					return Guard();
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(kAcquireRetryMs));
				continue;
			}

			// Phase 2 (HORS verrou) : ping. Si succes, on rend immediatement.
			if (mysql_ping(mysql_handle) == 0)
			{
				return Guard(this, mysql_handle);
			}

			// Phase 3 (HORS verrou) : ping a echoue. Close + reconnect.
			mysql_close(mysql_handle);
			MYSQL* new_handle = mysql_init(nullptr);
			bool reconnect_ok = false;
			if (new_handle)
			{
				bool reconnect_flag = false;
				mysql_options(new_handle, MYSQL_OPT_RECONNECT, &reconnect_flag);
				reconnect_ok = ConnectOne(new_handle);
				if (!reconnect_ok)
				{
					LOG_WARN(Core, "[ConnectionPool] Reconnect failed: {}", mysql_error(new_handle));
					mysql_close(new_handle);
					new_handle = nullptr;
				}
				else
				{
					LOG_INFO(Core, "[ConnectionPool] Reconnected after ping failure");
				}
			}

			// Phase 4 (verrou) : publier le nouveau handle (ou marquer le slot mort).
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (slot_index < m_connections.size())
				{
					if (reconnect_ok && new_handle)
					{
						m_connections[slot_index].mysql = new_handle;
						// in_use reste true : on remet la connexion au caller.
					}
					else
					{
						m_connections[slot_index].mysql = nullptr;
						m_connections[slot_index].in_use = false;
					}
				}
			}

			if (reconnect_ok && new_handle)
			{
				return Guard(this, new_handle);
			}

			if (std::chrono::steady_clock::now() >= deadline)
			{
				LOG_ERROR(Core, "[ConnectionPool] Acquire timeout after reconnect failure");
				return Guard();
			}
			// On boucle pour essayer un autre slot.
		}
	}

	void ConnectionPool::Release(MYSQL* mysql)
	{
		if (!mysql)
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& e : m_connections)
		{
			if (e.mysql == mysql)
			{
				e.in_use = false;
				return;
			}
		}
	}
}
