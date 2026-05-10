#include "src/shardd/pools/PoolManagerRuntime.h"

namespace engine::server::pools
{
	/// Enregistre deux pools V1 :
	///   - poolId 1 ("wolves") : 3 candidats (spawnIds 1001/1002/1003)
	///     avec poids 2.0 / 1.0 / 1.0, maxActive=1
	///   - poolId 2 ("rabbits") : 2 candidats (spawnIds 2001/2002)
	///     avec poids 1.0 / 1.0, maxActive=2 (les deux peuvent etre actifs)
	/// Les SpawnIds sont arbitraires : ils referencent une futur table
	/// creature_spawn dont les binding seront ajoutes en meme temps que
	/// le loader DB des pools.
	void PoolManagerRuntime::SeedV1Pools()
	{
		Pool wolves;
		wolves.poolId    = 1;
		wolves.maxActive = 1;
		wolves.entries   = {
			{ 1001, 2.0f },   // dire wolf : poids double
			{ 1002, 1.0f },   // grey wolf
			{ 1003, 1.0f }    // shadow wolf
		};
		m_mgr.Register(std::move(wolves));

		Pool rabbits;
		rabbits.poolId    = 2;
		rabbits.maxActive = 2;
		rabbits.entries   = {
			{ 2001, 1.0f },   // brown rabbit
			{ 2002, 1.0f }    // grey rabbit
		};
		m_mgr.Register(std::move(rabbits));
	}

	/// Delegue a PoolManager::Roll en passant m_rng par reference. Le RNG
	/// interne est seede une fois via random_device au constructeur ; ne
	/// pas le re-seeder en cours d'execution (briserait la sequence).
	std::vector<SpawnId> PoolManagerRuntime::Roll(PoolId poolId)
	{
		return m_mgr.Roll(poolId, m_rng);
	}
}
