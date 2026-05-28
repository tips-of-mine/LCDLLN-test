#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

struct MYSQL;

namespace engine::core
{
	class Config;
}

namespace engine::server::db
{
	class SqlPreparedStatementCache;

	/// Thread-safe MySQL connection pool: configurable size, health check (ping) and reconnect on Acquire.
	/// Only built on UNIX with MySQL client. Warm-up: Init() creates all connections at boot.
	///
	/// Chaque connexion porte un `SqlPreparedStatementCache` (LRU, taille fixe 32)
	/// dédié — accessible via `Guard::cache()`. Les `MYSQL_STMT*` cachés sont liés
	/// à la connexion qui les a préparés ; le cache est donc reset lors de la
	/// reconnexion d'un slot et lors du Shutdown.
	class ConnectionPool
	{
	public:
		ConnectionPool() = default;
		~ConnectionPool() = default;
		ConnectionPool(const ConnectionPool&) = delete;
		ConnectionPool& operator=(const ConnectionPool&) = delete;

		/// Initializes the pool from config (db.host, db.port, db.user, db.password, db.database, db.pool_size).
		/// Creates pool_size connections (warm-up). Returns true on success, false if DB not configured or connect failed.
		bool Init(const engine::core::Config& config);

		/// Closes all connections and clears the pool. Safe to call multiple times.
		void Shutdown();

		/// Returns true if the pool is initialized and has at least one connection.
		bool IsInitialized() const;

		/// RAII handle: holds a connection from the pool; returns it on destruction.
		class Guard
		{
		public:
			Guard() = default;
			Guard(ConnectionPool* pool, MYSQL* mysql, SqlPreparedStatementCache* cache);
			Guard(Guard&& other) noexcept;
			Guard& operator=(Guard&& other) noexcept;
			~Guard();
			Guard(const Guard&) = delete;
			Guard& operator=(const Guard&) = delete;

			/// Raw connection for use with DbExecute / DbQuery. Null if empty.
			MYSQL* get() const { return m_mysql; }
			explicit operator bool() const { return m_mysql != nullptr; }

			/// Cache de prepared statements dédié à cette connexion.
			/// Null si la connexion est vide.
			SqlPreparedStatementCache* cache() const { return m_cache; }

		private:
			ConnectionPool* m_pool = nullptr;
			MYSQL* m_mysql = nullptr;
			SqlPreparedStatementCache* m_cache = nullptr;
		};

		/// Acquires a connection (ping + reconnect if needed). Blocks until one is available or timeout.
		/// Returns an empty Guard if pool not initialized or all connections failed health check.
		Guard Acquire();

	private:
		struct Entry
		{
			MYSQL* mysql = nullptr;
			bool in_use = false;
			std::unique_ptr<SqlPreparedStatementCache> cache;
		};
		void Release(MYSQL* mysql);
		bool ConnectOne(MYSQL* mysql) const;

		mutable std::mutex m_mutex;
		std::vector<Entry> m_connections;
		std::string m_host;
		std::string m_user;
		std::string m_password;
		std::string m_database;
		unsigned int m_port = 3306;
		bool m_initialized = false;
	};
}
