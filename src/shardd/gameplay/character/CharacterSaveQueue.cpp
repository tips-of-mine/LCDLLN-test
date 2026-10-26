// ETAPE2.2 — CharacterSaveQueue : voir CharacterSaveQueue.h.

#include "src/shardd/gameplay/character/CharacterSaveQueue.h"

#include <algorithm>

namespace engine::server
{
	CharacterSaveQueue::CharacterSaveQueue(size_t workerCount, size_t maxPending,
		ThreadHook onThreadStart, ThreadHook onThreadEnd)
		: m_maxPending(maxPending == 0u ? 1u : maxPending)
		, m_onThreadStart(std::move(onThreadStart))
		, m_onThreadEnd(std::move(onThreadEnd))
	{
		const size_t count = std::clamp<size_t>(workerCount, 1u, 16u);
		m_workers.reserve(count);
		for (size_t i = 0; i < count; ++i)
		{
			m_workers.push_back(std::make_unique<Worker>());
		}
	}

	CharacterSaveQueue::~CharacterSaveQueue()
	{
		Stop();
	}

	void CharacterSaveQueue::Start()
	{
		if (m_running.exchange(true))
		{
			return;
		}
		for (auto& worker : m_workers)
		{
			{
				std::lock_guard<std::mutex> lock(worker->mutex);
				worker->accepting = true;
				worker->stopping = false;
			}
			Worker* raw = worker.get();
			worker->thread = std::thread([this, raw] { WorkerLoop(*raw); });
		}
	}

	void CharacterSaveQueue::Stop()
	{
		if (!m_running.exchange(false))
		{
			return;
		}
		for (auto& worker : m_workers)
		{
			{
				std::lock_guard<std::mutex> lock(worker->mutex);
				worker->accepting = false;
				worker->stopping = true;
			}
			worker->wake.notify_all();
		}
		for (auto& worker : m_workers)
		{
			if (worker->thread.joinable())
			{
				worker->thread.join();
			}
		}
	}

	bool CharacterSaveQueue::IsRunning() const
	{
		return m_running.load();
	}

	CharacterSaveQueue::EnqueueResult CharacterSaveQueue::EnqueueSave(uint64_t characterId, Job job)
	{
		return Enqueue(characterId, std::move(job), true);
	}

	CharacterSaveQueue::EnqueueResult CharacterSaveQueue::EnqueueOrdered(uint64_t characterId, Job job)
	{
		return Enqueue(characterId, std::move(job), false);
	}

	CharacterSaveQueue::EnqueueResult CharacterSaveQueue::Enqueue(uint64_t characterId, Job job, bool isSave)
	{
		Worker& worker = *m_workers[WorkerIndexFor(characterId, m_workers.size())];
		{
			std::lock_guard<std::mutex> lock(worker.mutex);
			if (!worker.accepting || !job)
			{
				m_rejected.fetch_add(1);
				return EnqueueResult::Rejected;
			}
			if (isSave)
			{
				// Dernier travail en attente de ce personnage : si c'est une sauvegarde,
				// on la remplace (l'état le plus récent suffit, l'ordre est conservé).
				for (auto it = worker.queue.rbegin(); it != worker.queue.rend(); ++it)
				{
					if (it->characterId != characterId)
					{
						continue;
					}
					if (it->isSave)
					{
						it->job = std::move(job);
						m_coalesced.fetch_add(1);
						return EnqueueResult::Coalesced;
					}
					break; // travail ordonné plus récent : on ne le double pas
				}
			}
			if (m_pending.load() >= m_maxPending)
			{
				m_rejected.fetch_add(1);
				return EnqueueResult::Rejected;
			}
			worker.queue.push_back(Entry{ characterId, std::move(job), isSave });
			m_pending.fetch_add(1);
			m_queued.fetch_add(1);
		}
		worker.wake.notify_one();
		return EnqueueResult::Queued;
	}

	void CharacterSaveQueue::WorkerLoop(Worker& worker)
	{
		if (m_onThreadStart)
		{
			m_onThreadStart();
		}
		while (true)
		{
			Entry entry;
			{
				std::unique_lock<std::mutex> lock(worker.mutex);
				worker.wake.wait(lock, [&worker] { return worker.stopping || !worker.queue.empty(); });
				if (worker.queue.empty())
				{
					break; // arrêt demandé et file vide
				}
				entry = std::move(worker.queue.front());
				worker.queue.pop_front();
				worker.busy = true;
				worker.busyCharacterId = entry.characterId;
			}
			entry.job();
			m_executed.fetch_add(1);
			m_pending.fetch_sub(1);
			{
				std::lock_guard<std::mutex> lock(worker.mutex);
				worker.busy = false;
				worker.busyCharacterId = 0;
			}
			worker.done.notify_all();
		}
		if (m_onThreadEnd)
		{
			m_onThreadEnd();
		}
	}

	bool CharacterSaveQueue::Flush(uint64_t characterId, std::chrono::milliseconds timeout)
	{
		if (!m_running.load())
		{
			return true;
		}
		Worker& worker = *m_workers[WorkerIndexFor(characterId, m_workers.size())];
		std::unique_lock<std::mutex> lock(worker.mutex);
		return worker.done.wait_for(lock, timeout, [&worker, characterId] {
			if (worker.busy && worker.busyCharacterId == characterId)
			{
				return false;
			}
			return std::none_of(worker.queue.begin(), worker.queue.end(),
				[characterId](const Entry& e) { return e.characterId == characterId; });
		});
	}

	void CharacterSaveQueue::FlushAll()
	{
		if (!m_running.load())
		{
			return;
		}
		for (auto& worker : m_workers)
		{
			std::unique_lock<std::mutex> lock(worker->mutex);
			worker->done.wait(lock, [&worker] { return !worker->busy && worker->queue.empty(); });
		}
	}

	size_t CharacterSaveQueue::PendingCount() const
	{
		return m_pending.load();
	}

	bool CharacterSaveQueue::IsOverloaded() const
	{
		return m_pending.load() * 4u >= m_maxPending * 3u;
	}

	CharacterSaveQueue::Stats CharacterSaveQueue::GetStats() const
	{
		Stats stats{};
		stats.queued = m_queued.load();
		stats.coalesced = m_coalesced.load();
		stats.rejected = m_rejected.load();
		stats.executed = m_executed.load();
		return stats;
	}
}
