#include "src/shared/db/SqlDelayThread.h"

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

namespace engine::server::db
{
	SqlDelayThread::SqlDelayThread(ConnectionPool& pool, size_t maxQueueSize)
		: m_pool(pool)
		, m_maxQueueSize(maxQueueSize)
	{
	}

	SqlDelayThread::~SqlDelayThread()
	{
		// Si l'utilisateur a oublié Stop(), on l'appelle ici pour ne pas crasher.
		if (m_running.load())
			Stop();
	}

	void SqlDelayThread::Start()
	{
		if (m_running.exchange(true))
			return;  // déjà démarré
		m_stopRequested.store(false);
		m_thread = std::thread(&SqlDelayThread::WorkerLoop, this);
	}

	void SqlDelayThread::Stop()
	{
		if (!m_running.load())
			return;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopRequested.store(true);
		}
		m_cv.notify_all();
		if (m_thread.joinable())
			m_thread.join();
		m_running.store(false);
	}

	bool SqlDelayThread::EnqueueExecute(std::string sql, ExecuteCallback callback)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stopRequested.load())
				return false;  // refuse les enqueues post-Stop (CMANGOS.13 review #5)
			if (m_queue.size() >= m_maxQueueSize)
				return false;
			m_queue.push_back(Job{std::move(sql), std::move(callback)});
		}
		m_cv.notify_one();
		return true;
	}

	size_t SqlDelayThread::QueueSize() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

	void SqlDelayThread::WorkerLoop()
	{
		mysql_thread_init();  // libmysql per-thread init (CMANGOS.13 review #2)
		while (true)
		{
			Job job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait(lock, [this] {
					return m_stopRequested.load() || !m_queue.empty();
				});
				// Drain : on traite tout ce qui est en queue avant de quitter.
				if (m_queue.empty())
				{
					if (m_stopRequested.load())
					{
						mysql_thread_end();
						return;
					}
					continue;
				}
				job = std::move(m_queue.front());
				m_queue.pop_front();
			}

			// Exécution hors lock.
			auto guard = m_pool.Acquire();
			MYSQL* mysql = guard.get();
			const bool ok = mysql && DbExecute(mysql, job.sql);
			if (job.callback)
				job.callback(ok);
		}
	}
}
