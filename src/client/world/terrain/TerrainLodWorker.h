#pragma once

#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/terrain/TerrainLodChain.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace engine::world::terrain
{
	/// Thread pool générant la chaîne LOD pour un chunk en arrière-plan
	/// (M100.8). Contraintes : main thread jamais bloqué > 1 ms par
	/// l'`Enqueue` (ajout en queue + notify_one, retour immédiat).
	///
	/// Stale jobs : chaque enqueue sur un même `coord` incrémente la
	/// génération du chunk. Un job dont la génération a été dépassée
	/// au moment de la fin de calcul est jeté (pas de callback). Cela évite
	/// d'écrire un `terrain_lods.bin` correspondant à une heightmap périmée
	/// quand l'utilisateur enchaîne plusieurs brushstrokes rapides.
	///
	/// Le callback `onResult` est invoqué sur un thread worker. Le caller
	/// est responsable de re-router au main thread si nécessaire.
	class TerrainLodWorker
	{
	public:
		~TerrainLodWorker();

		/// Démarre `workerCount` threads de travail. No-op si déjà démarré.
		void Start(uint32_t workerCount);

		/// Arrêt ordonné : signale stop, notifie tous les threads, joint les
		/// threads, vide la queue. Idempotent.
		void Stop();

		/// Pousse un job de génération LOD pour `coord` à partir de `lod0`.
		/// Le `lod0` est copié pour isoler le job du main thread (le caller
		/// peut continuer à éditer la heightmap sans data race).
		/// \param onResult callback appelé sur un worker thread quand la chain
		/// est prête, sauf si le job a été staled (génération dépassée).
		/// Effet de bord : incrémente la génération de `coord`.
		void Enqueue(engine::world::GlobalChunkCoord coord,
			TerrainChunk lod0,
			std::function<void(engine::world::GlobalChunkCoord, TerrainLodChain)> onResult);

	private:
		struct Job
		{
			engine::world::GlobalChunkCoord coord{0, 0};
			TerrainChunk lod0;
			uint64_t generation = 0;
			std::function<void(engine::world::GlobalChunkCoord, TerrainLodChain)> onResult;
		};

		void WorkerLoop();
		uint64_t IncrementGeneration(engine::world::GlobalChunkCoord coord);
		uint64_t GetGeneration(engine::world::GlobalChunkCoord coord) const;
		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);

		std::vector<std::thread> m_workers;
		std::deque<Job> m_queue;
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		std::atomic<bool> m_stopRequested{false};
		mutable std::mutex m_genMutex;
		std::unordered_map<uint64_t, uint64_t> m_generations;
	};
}
