#pragma once
// CMANGOS.13 (Phase 1a) — SqlDelayThread : worker thread async qui consomme
// une queue d'opérations DB avec callbacks. Permet aux handlers de "fire and
// forget" un INSERT/UPDATE sans bloquer le tick.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct MYSQL;

namespace engine::server::db
{
	class ConnectionPool;

	/// Worker thread async pour exécution DB hors tick.
	///
	/// Usage type :
	/// ```cpp
	/// SqlDelayThread worker(pool, 1024);
	/// worker.Start();
	/// // ... plus tard, dans un handler chaud :
	/// worker.EnqueueExecute("INSERT INTO audit (...) VALUES (...)",
	///     [](bool ok) { if (!ok) LOG_WARN(Core, "audit failed"); });
	/// // ... à la fin du process :
	/// worker.Stop();
	/// ```
	///
	/// Thread-safety : Start/Stop ne doivent être appelés qu'une fois (depuis le
	/// même thread). Enqueue* est thread-safe.
	class SqlDelayThread
	{
	public:
		using ExecuteCallback = std::function<void(bool ok)>;

		/// Construit le worker. \p maxQueueSize est la capacité max avant rejet.
		SqlDelayThread(ConnectionPool& pool, size_t maxQueueSize);
		~SqlDelayThread();
		SqlDelayThread(const SqlDelayThread&) = delete;
		SqlDelayThread& operator=(const SqlDelayThread&) = delete;

		/// Démarre le worker. À appeler une fois.
		void Start();

		/// Arrête le worker proprement : drain la queue restante, puis join.
		/// À appeler une fois en fin de vie.
		void Stop();

		/// Enqueue un INSERT/UPDATE/DELETE asynchrone. \p callback peut être null
		/// (fire-and-forget). Retourne false si la queue est pleine (politique
		/// reject, le caller décide).
		bool EnqueueExecute(std::string sql, ExecuteCallback callback);

		/// Taille actuelle de la queue (snapshot, peut bouger immédiatement).
		size_t QueueSize() const;

	private:
		struct Job
		{
			std::string sql;
			ExecuteCallback callback;
		};

		void WorkerLoop();

		ConnectionPool& m_pool;
		const size_t m_maxQueueSize;
		std::thread m_thread;
		std::atomic<bool> m_running{false};
		std::atomic<bool> m_stopRequested{false};
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		std::deque<Job> m_queue;
	};
}
