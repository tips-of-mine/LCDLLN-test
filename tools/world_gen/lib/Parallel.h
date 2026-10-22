#pragma once

// tools/world_gen — parallélisation DÉTERMINISTE des boucles par rangées.
//
// Chaque rangée est calculée par une fonction pure de son indice et n'écrit que
// dans ses propres cases : le résultat ne dépend ni du nombre de threads ni de
// l'ordre d'exécution (sorties identiques octet pour octet).

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace tools::world_gen
{
	/// Appelle \p fn(row) pour row ∈ [0, rows) sur tous les cœurs disponibles.
	/// \p fn ne doit écrire QUE dans des données propres à sa rangée.
	template <typename F>
	void ParallelRows(int rows, F&& fn)
	{
		const unsigned hw = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
		if (rows < 64 || hw == 1u)
		{
			for (int r = 0; r < rows; ++r) fn(r);
			return;
		}
		std::atomic<int> next{ 0 };
		std::vector<std::thread> pool;
		pool.reserve(hw);
		for (unsigned t = 0; t < hw; ++t)
			pool.emplace_back([&]() {
				for (int r = next.fetch_add(1); r < rows; r = next.fetch_add(1)) fn(r);
			});
		for (std::thread& th : pool) th.join();
	}
}
