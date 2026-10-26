// ETAPE2.2 — tests de CharacterSaveQueue (sans base de données).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/gameplay/character/CharacterSaveQueue.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	using engine::server::CharacterSaveQueue;
}

int main()
{
	// 1. Routage : même personnage -> même thread.
	Check(CharacterSaveQueue::WorkerIndexFor(10, 4) == 2u, "10 % 4 = 2");
	Check(CharacterSaveQueue::WorkerIndexFor(7, 1) == 0u, "un seul thread");
	Check(CharacterSaveQueue::WorkerIndexFor(7, 0) == 0u, "zéro thread : pas de division par zéro");

	// 2. Queue arrêtée : refus (l'appelant exécute lui-même).
	{
		CharacterSaveQueue queue(2, 16);
		Check(queue.EnqueueSave(1, [] {}) == CharacterSaveQueue::EnqueueResult::Rejected, "refus avant Start");
	}

	// 3. Ordre conservé pour un personnage, fusion des sauvegardes en attente.
	{
		std::mutex gate;
		gate.lock(); // bloque le premier travail tant que tout n'est pas déposé
		std::mutex logMutex;
		std::vector<int> order;
		CharacterSaveQueue queue(1, 64);
		queue.Start();
		Check(queue.EnqueueOrdered(5, [&] { std::lock_guard<std::mutex> g(gate); std::lock_guard<std::mutex> l(logMutex); order.push_back(0); })
			== CharacterSaveQueue::EnqueueResult::Queued, "premier travail en file");
		// Laisse le thread prendre le premier travail (bloqué sur gate).
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		Check(queue.EnqueueSave(5, [&] { std::lock_guard<std::mutex> l(logMutex); order.push_back(1); })
			== CharacterSaveQueue::EnqueueResult::Queued, "sauvegarde 1 en file");
		Check(queue.EnqueueSave(5, [&] { std::lock_guard<std::mutex> l(logMutex); order.push_back(2); })
			== CharacterSaveQueue::EnqueueResult::Coalesced, "sauvegarde 2 remplace la 1");
		Check(queue.EnqueueOrdered(5, [&] { std::lock_guard<std::mutex> l(logMutex); order.push_back(3); })
			== CharacterSaveQueue::EnqueueResult::Queued, "travail ordonné après");
		Check(queue.EnqueueSave(5, [&] { std::lock_guard<std::mutex> l(logMutex); order.push_back(4); })
			== CharacterSaveQueue::EnqueueResult::Queued, "sauvegarde après un travail ordonné : pas de fusion");
		gate.unlock();
		queue.Flush(5);
		std::lock_guard<std::mutex> l(logMutex);
		Check(order == std::vector<int>({ 0, 2, 3, 4 }), "ordre 0,2,3,4 (la 1 a été remplacée)");
		const auto stats = queue.GetStats();
		Check(stats.coalesced == 1u && stats.executed == 4u, "compteurs");
		queue.Stop();
	}

	// 4. Capacité : au-delà, refus ; Stop exécute ce qui reste.
	{
		std::mutex gate;
		gate.lock();
		std::atomic<int> done{0};
		CharacterSaveQueue queue(1, 2);
		queue.Start();
		Check(queue.EnqueueOrdered(1, [&] { std::lock_guard<std::mutex> g(gate); ++done; })
			== CharacterSaveQueue::EnqueueResult::Queued, "capacité : 1");
		Check(queue.EnqueueOrdered(2, [&] { ++done; }) == CharacterSaveQueue::EnqueueResult::Queued, "capacité : 2");
		Check(queue.IsOverloaded(), "surcharge à 100 %");
		Check(queue.EnqueueOrdered(3, [&] { ++done; }) == CharacterSaveQueue::EnqueueResult::Rejected, "capacité dépassée : refus");
		gate.unlock();
		queue.Stop();
		Check(done.load() == 2, "Stop exécute les travaux en attente");
		Check(queue.PendingCount() == 0u, "plus rien en attente");
		Check(queue.EnqueueSave(1, [] {}) == CharacterSaveQueue::EnqueueResult::Rejected, "refus après Stop");
	}

	// 5. Plusieurs threads, plusieurs personnages : chaque personnage dans l'ordre.
	{
		constexpr int kCharacters = 8;
		constexpr int kJobs = 200;
		std::vector<std::vector<int>> seen(kCharacters);
		std::vector<std::mutex> locks(kCharacters);
		CharacterSaveQueue queue(3, 100000);
		queue.Start();
		for (int j = 0; j < kJobs; ++j)
		{
			for (int c = 0; c < kCharacters; ++c)
			{
				queue.EnqueueOrdered(static_cast<uint64_t>(c), [&, c, j] {
					std::lock_guard<std::mutex> g(locks[c]);
					seen[c].push_back(j);
				});
			}
		}
		queue.FlushAll();
		bool ordered = true;
		for (int c = 0; c < kCharacters; ++c)
		{
			if (seen[c].size() != static_cast<size_t>(kJobs))
				ordered = false;
			for (size_t i = 1; i < seen[c].size(); ++i)
				if (seen[c][i] < seen[c][i - 1])
					ordered = false;
		}
		Check(ordered, "ordre par personnage conservé avec 3 threads");
		queue.Stop();
	}

	// 6. Crochets de thread appelés une fois par thread.
	{
		std::atomic<int> starts{0};
		std::atomic<int> ends{0};
		{
			CharacterSaveQueue queue(2, 8, [&] { ++starts; }, [&] { ++ends; });
			queue.Start();
			queue.Stop();
		}
		Check(starts.load() == 2 && ends.load() == 2, "crochets start/end par thread");
	}

	if (s_failCount == 0)
		std::puts("[OK] character_save_queue_tests");
	return s_failCount == 0 ? 0 : 1;
}
