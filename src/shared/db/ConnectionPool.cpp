// M21.6 — Connection pool: configurable size, health check (ping), reconnect on Acquire. Thread-safe.

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <chrono>
#include <thread>

namespace engine::server::db
{
	namespace
	{
		constexpr int kAcquireTimeoutMs = 5000;
		constexpr int kAcquireRetryMs = 50;
		/// Capacité LRU du cache de prepared statements par connexion.
		/// 32 = couvre confortablement les hot handlers du master sans gaspiller
		/// de mémoire (chaque entrée ≈ 1 MYSQL_STMT + buffers de bind ~ quelques Ko).
		constexpr size_t kPreparedStatementCacheSize = 32;
	}

	ConnectionPool::Guard::Guard(ConnectionPool* pool, MYSQL* mysql, SqlPreparedStatementCache* cache)
		: m_pool(pool), m_mysql(mysql), m_cache(cache) {}

	ConnectionPool::Guard::Guard(Guard&& other) noexcept
		: m_pool(other.m_pool), m_mysql(other.m_mysql), m_cache(other.m_cache)
	{
		other.m_pool = nullptr;
		other.m_mysql = nullptr;
		other.m_cache = nullptr;
	}

	ConnectionPool::Guard& ConnectionPool::Guard::operator=(Guard&& other) noexcept
	{
		if (this != &other)
		{
			if (m_pool && m_mysql)
				m_pool->Release(m_mysql);
			m_pool = other.m_pool;
			m_mysql = other.m_mysql;
			m_cache = other.m_cache;
			other.m_pool = nullptr;
			other.m_mysql = nullptr;
			other.m_cache = nullptr;
		}
		return *this;
	}

	ConnectionPool::Guard::~Guard()
	{
		if (m_pool && m_mysql)
			m_pool->Release(m_mysql);
		m_pool = nullptr;
		m_mysql = nullptr;
		m_cache = nullptr;
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
			m_connections[i].cache = std::make_unique<SqlPreparedStatementCache>(kPreparedStatementCacheSize);
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
				// Détruire le cache de prepared statements AVANT de fermer la
				// connexion : les ~SqlPreparedStatement appellent mysql_stmt_close
				// qui requiert que le MYSQL* parent soit encore valide.
				e.cache.reset();
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
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kAcquireTimeoutMs);
		for (;;)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_initialized)
					return Guard();

				for (auto& e : m_connections)
				{
					if (!e.in_use && e.mysql)
					{
						if (mysql_ping(e.mysql) != 0)
						{
							// Reset du cache de prepared statements AVANT mysql_close :
							// les MYSQL_STMT cachés sont liés à la connexion parente,
							// on doit les fermer tant que le MYSQL* est encore valide.
							e.cache.reset();
							mysql_close(e.mysql);
							e.mysql = mysql_init(nullptr);
							if (e.mysql)
							{
								bool reconnect = false;
								mysql_options(e.mysql, MYSQL_OPT_RECONNECT, &reconnect);
								if (!ConnectOne(e.mysql))
								{
									LOG_WARN(Core, "[ConnectionPool] Reconnect failed: {}", mysql_error(e.mysql));
									mysql_close(e.mysql);
									e.mysql = nullptr;
									continue;
								}
								// Nouveau cache pour la connexion reconnectée :
								// les anciens stmts étaient liés au MYSQL* fermé.
								e.cache = std::make_unique<SqlPreparedStatementCache>(kPreparedStatementCacheSize);
								LOG_INFO(Core, "[ConnectionPool] Reconnected after ping failure");
							}
						}
						if (e.mysql)
						{
							e.in_use = true;
							return Guard(this, e.mysql, e.cache.get());
						}
					}
				}
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				LOG_ERROR(Core, "[ConnectionPool] Acquire timeout");
				return Guard();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(kAcquireRetryMs));
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
