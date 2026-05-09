#pragma once
// CMANGOS.22 (Phase 4.22a) — PoolManager : weighted random spawn pour
// rare spawns. Une pool a N candidats avec poids et un slot maxActive.
// L'algorithm choisit weighted-random sans replacement parmi les
// candidats.

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace engine::server::pools
{
	using PoolId   = uint32_t;
	using SpawnId  = uint32_t;

	struct PoolEntry
	{
		SpawnId spawnId = 0;
		float   weight  = 1.0f;  ///< plus eleve = plus probable
	};

	struct Pool
	{
		PoolId                 poolId     = 0;
		uint32_t               maxActive  = 1;
		std::vector<PoolEntry> entries;
	};

	class PoolManager
	{
	public:
		PoolManager() = default;

		void Register(Pool pool) { m_pools[pool.poolId] = std::move(pool); }

		/// Roll \p count spawns parmi la pool \p poolId. Retourne les
		/// SpawnIds choisis (sans replacement). Si count > entries.size(),
		/// retourne tous les entries. Si pool inconnue ou maxActive=0,
		/// retourne vide.
		std::vector<SpawnId> Roll(PoolId poolId, std::mt19937& rng) const
		{
			auto it = m_pools.find(poolId);
			if (it == m_pools.end()) return {};
			const Pool& p = it->second;
			if (p.maxActive == 0 || p.entries.empty()) return {};

			// Copy + weighted shuffle.
			std::vector<PoolEntry> copy = p.entries;
			std::vector<SpawnId> out;
			out.reserve(std::min<size_t>(p.maxActive, copy.size()));

			for (uint32_t i = 0; i < p.maxActive && !copy.empty(); ++i)
			{
				float total = 0.0f;
				for (const auto& e : copy) total += std::max(0.0f, e.weight);
				if (total <= 0.0f) break;

				std::uniform_real_distribution<float> d(0.0f, total);
				const float pick = d(rng);
				float acc = 0.0f;
				size_t pickedIdx = 0;
				for (size_t k = 0; k < copy.size(); ++k)
				{
					acc += std::max(0.0f, copy[k].weight);
					if (pick <= acc) { pickedIdx = k; break; }
				}
				out.push_back(copy[pickedIdx].spawnId);
				copy.erase(copy.begin() + pickedIdx);  // sans replacement
			}
			return out;
		}

		size_t PoolCount() const noexcept { return m_pools.size(); }

	private:
		std::unordered_map<PoolId, Pool> m_pools;
	};
}
