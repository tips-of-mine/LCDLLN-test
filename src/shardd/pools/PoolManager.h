#pragma once
// CMANGOS.22 — PoolManager : weighted random spawn pour rare spawns.
// Une pool a N candidats avec poids et un slot maxActive. L'algorithme
// choisit weighted-random sans replacement parmi les candidats.
//
// Phase 4.22a (mergee) : pools simples (entries = SpawnId).
// Wave 20 : nested pools — une entry peut etre un SpawnId OU une
// reference vers un autre PoolId (table pool_pool). Roll() recurse
// avec cycle detection (visited set + profondeur max).

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <unordered_set>
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

	/// Wave 20 : entry "nested" — pointe vers un autre pool. Quand pickee,
	/// Roll() recurse dans le pool enfant et flatten le resultat.
	struct NestedPoolEntry
	{
		PoolId childPoolId = 0;
		float  weight      = 1.0f;
	};

	struct Pool
	{
		PoolId                       poolId    = 0;
		uint32_t                     maxActive = 1;
		std::vector<PoolEntry>       entries;  ///< spawns directs
		std::vector<NestedPoolEntry> nested;   ///< sous-pools (Wave 20)
	};

	/// Profondeur maximale de recursion pour les nested pools. Si depassee,
	/// la branche nestee est skip silent (defense contre cycles non
	/// detectables ou pyramides excessives).
	inline constexpr uint32_t kMaxNestedPoolDepth = 8;

	class PoolManager
	{
	public:
		PoolManager() = default;

		void Register(Pool pool) { m_pools[pool.poolId] = std::move(pool); }

		/// Roll \p count spawns parmi la pool \p poolId. Retourne les
		/// SpawnIds choisis (sans replacement pour entries directs ; les
		/// nested pools sont independants au sein de leur propre Roll).
		///
		/// Si une nested entry est pickee, recurse dans le pool enfant.
		/// Cycle detection via visited set + depth limit (kMaxNestedPoolDepth).
		///
		/// Compatible avec les pools "simples" (nested vide) : comportement
		/// identique a la Phase 4.22a — backward compat strict.
		std::vector<SpawnId> Roll(PoolId poolId, std::mt19937& rng) const
		{
			std::unordered_set<PoolId> visited;
			std::vector<SpawnId> out;
			RollImpl(poolId, rng, /*depth*/0, visited, out);
			return out;
		}

		size_t PoolCount() const noexcept { return m_pools.size(); }

	private:
		/// Recursion interne avec cycle detection.
		void RollImpl(PoolId poolId, std::mt19937& rng, uint32_t depth,
			std::unordered_set<PoolId>& visited, std::vector<SpawnId>& out) const
		{
			// Cycle ou profondeur excessive : skip silent.
			if (depth >= kMaxNestedPoolDepth) return;
			if (!visited.insert(poolId).second) return; // deja visite -> cycle

			auto it = m_pools.find(poolId);
			if (it == m_pools.end()) return;
			const Pool& p = it->second;
			if (p.maxActive == 0) return;
			if (p.entries.empty() && p.nested.empty()) return;

			// Combiner entries + nested dans une liste de "candidats" tiree
			// sans replacement. Tag : Spawn ou Nested.
			enum class Kind { Spawn, Nested };
			struct Candidate { Kind kind; SpawnId spawnId; PoolId childPoolId; float weight; };
			std::vector<Candidate> candidates;
			candidates.reserve(p.entries.size() + p.nested.size());
			for (const auto& e : p.entries)
				candidates.push_back({Kind::Spawn, e.spawnId, 0, std::max(0.0f, e.weight)});
			for (const auto& n : p.nested)
				candidates.push_back({Kind::Nested, 0, n.childPoolId, std::max(0.0f, n.weight)});

			for (uint32_t i = 0; i < p.maxActive && !candidates.empty(); ++i)
			{
				float total = 0.0f;
				for (const auto& c : candidates) total += c.weight;
				if (total <= 0.0f) break;

				std::uniform_real_distribution<float> d(0.0f, total);
				const float pick = d(rng);
				float acc = 0.0f;
				size_t pickedIdx = 0;
				for (size_t k = 0; k < candidates.size(); ++k)
				{
					acc += candidates[k].weight;
					if (pick <= acc) { pickedIdx = k; break; }
				}

				const Candidate& chosen = candidates[pickedIdx];
				if (chosen.kind == Kind::Spawn)
				{
					out.push_back(chosen.spawnId);
				}
				else // Nested
				{
					RollImpl(chosen.childPoolId, rng, depth + 1, visited, out);
				}
				candidates.erase(candidates.begin() + pickedIdx);  // sans replacement
			}
		}

		std::unordered_map<PoolId, Pool> m_pools;
	};
}
