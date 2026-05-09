#include "src/client/world/terrain/TerrainLodWorker.h"

#include <utility>

namespace engine::world::terrain
{
	TerrainLodWorker::~TerrainLodWorker()
	{
		Stop();
	}

	uint64_t TerrainLodWorker::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
			 | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	void TerrainLodWorker::Start(uint32_t workerCount)
	{
		if (!m_workers.empty()) return; // déjà démarré
		m_stopRequested.store(false);
		m_workers.reserve(workerCount);
		for (uint32_t i = 0; i < workerCount; ++i)
			m_workers.emplace_back([this] { this->WorkerLoop(); });
	}

	void TerrainLodWorker::Stop()
	{
		if (m_workers.empty()) return;
		m_stopRequested.store(true);
		m_cv.notify_all();
		for (auto& t : m_workers)
			if (t.joinable()) t.join();
		m_workers.clear();
		std::lock_guard lk(m_mutex);
		m_queue.clear();
	}

	uint64_t TerrainLodWorker::IncrementGeneration(engine::world::GlobalChunkCoord coord)
	{
		std::lock_guard lk(m_genMutex);
		return ++m_generations[PackCoord(coord)];
	}

	uint64_t TerrainLodWorker::GetGeneration(engine::world::GlobalChunkCoord coord) const
	{
		std::lock_guard lk(m_genMutex);
		auto it = m_generations.find(PackCoord(coord));
		return (it == m_generations.end()) ? 0u : it->second;
	}

	void TerrainLodWorker::Enqueue(engine::world::GlobalChunkCoord coord,
		TerrainChunk lod0,
		std::function<void(engine::world::GlobalChunkCoord, TerrainLodChain)> onResult)
	{
		const uint64_t gen = IncrementGeneration(coord);
		Job job;
		job.coord = coord;
		job.lod0 = std::move(lod0);
		job.generation = gen;
		job.onResult = std::move(onResult);
		{
			std::lock_guard lk(m_mutex);
			m_queue.emplace_back(std::move(job));
		}
		m_cv.notify_one();
	}

	void TerrainLodWorker::WorkerLoop()
	{
		while (!m_stopRequested.load())
		{
			Job job;
			{
				std::unique_lock lk(m_mutex);
				m_cv.wait(lk, [this] { return m_stopRequested.load() || !m_queue.empty(); });
				if (m_stopRequested.load()) return;
				job = std::move(m_queue.front());
				m_queue.pop_front();
			}
			TerrainLodChain chain = GenerateLodChain(job.lod0);
			// Stale check : si la génération a avancé pendant le calcul,
			// l'utilisateur a relancé un sculpt → résultat périmé, on jette.
			if (GetGeneration(job.coord) != job.generation) continue;
			if (job.onResult) job.onResult(job.coord, std::move(chain));
		}
	}
}
