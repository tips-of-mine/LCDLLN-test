// Wave 20 — Pool persistence tests : InMemoryPoolStateStore round-trip
// + LoadByPool filtering. L'impl MySQL sera testee via integration
// tests separes (exclus de la CI Linux par exemple, ou couverts par
// db_layer_tests).

#include "src/shardd/pools/PoolStatePersistence.h"
#include "src/shared/core/Log.h"

#include <algorithm>

namespace
{
	using engine::server::pools::InMemoryPoolStateStore;
	using engine::server::pools::PoolMemberState;
	using engine::server::pools::PoolSpawnStatus;

	/// Empty store : LoadAll vide, LoadByPool vide.
	bool TestEmptyStore()
	{
		InMemoryPoolStateStore store;
		if (!store.LoadAll().empty()) return false;
		if (!store.LoadByPool(42).empty()) return false;
		if (store.Size() != 0) return false;
		LOG_INFO(Core, "[PoolPersistenceTests] empty store OK");
		return true;
	}

	/// Save 3 entries -> LoadAll retourne 3 entries identiques (champs preserves).
	bool TestSaveLoadRoundtrip()
	{
		InMemoryPoolStateStore store;
		std::vector<PoolMemberState> in = {
			{1, 100, PoolSpawnStatus::Alive,      0},
			{1, 101, PoolSpawnStatus::Dead,       1000000},
			{2, 200, PoolSpawnStatus::Respawning, 2000000},
		};
		if (!store.SaveAll(in)) return false;
		if (store.Size() != 3) return false;

		auto out = store.LoadAll();
		if (out.size() != 3) return false;

		// Tri pour comparaison deterministe.
		std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
			if (a.poolId != b.poolId) return a.poolId < b.poolId;
			return a.spawnId < b.spawnId;
		});

		if (out[0].poolId != 1 || out[0].spawnId != 100
			|| out[0].status != PoolSpawnStatus::Alive
			|| out[0].respawnAtSec != 0) return false;
		if (out[1].poolId != 1 || out[1].spawnId != 101
			|| out[1].status != PoolSpawnStatus::Dead
			|| out[1].respawnAtSec != 1000000) return false;
		if (out[2].poolId != 2 || out[2].spawnId != 200
			|| out[2].status != PoolSpawnStatus::Respawning
			|| out[2].respawnAtSec != 2000000) return false;
		LOG_INFO(Core, "[PoolPersistenceTests] save/load roundtrip OK");
		return true;
	}

	/// Upsert : sauver 2x le meme (poolId, spawnId) -> 1 entry, derniere
	/// valeur preservee.
	bool TestUpsertOverwrite()
	{
		InMemoryPoolStateStore store;
		std::vector<PoolMemberState> first = {
			{1, 100, PoolSpawnStatus::Alive, 0},
		};
		std::vector<PoolMemberState> second = {
			{1, 100, PoolSpawnStatus::Dead, 9999},
		};
		store.SaveAll(first);
		store.SaveAll(second);

		if (store.Size() != 1) return false;
		auto out = store.LoadAll();
		if (out.size() != 1) return false;
		if (out[0].status != PoolSpawnStatus::Dead) return false;
		if (out[0].respawnAtSec != 9999) return false;
		LOG_INFO(Core, "[PoolPersistenceTests] upsert overwrite OK");
		return true;
	}

	/// LoadByPool filtre par pool_id : seuls les entries de ce pool sont retournes.
	bool TestLoadByPool()
	{
		InMemoryPoolStateStore store;
		store.SaveAll({
			{1, 100, PoolSpawnStatus::Alive, 0},
			{1, 101, PoolSpawnStatus::Dead, 100},
			{2, 200, PoolSpawnStatus::Alive, 0},
			{2, 201, PoolSpawnStatus::Dead, 200},
			{3, 300, PoolSpawnStatus::Respawning, 300},
		});

		auto pool1 = store.LoadByPool(1);
		if (pool1.size() != 2) return false;
		for (const auto& s : pool1)
			if (s.poolId != 1) return false;

		auto pool2 = store.LoadByPool(2);
		if (pool2.size() != 2) return false;

		auto pool3 = store.LoadByPool(3);
		if (pool3.size() != 1) return false;
		if (pool3[0].status != PoolSpawnStatus::Respawning) return false;

		auto pool42 = store.LoadByPool(42);
		if (!pool42.empty()) return false;
		LOG_INFO(Core, "[PoolPersistenceTests] LoadByPool filter OK");
		return true;
	}

	/// Clear : vide tout, Size() retombe a 0.
	bool TestClear()
	{
		InMemoryPoolStateStore store;
		store.SaveAll({
			{1, 100, PoolSpawnStatus::Alive, 0},
			{1, 101, PoolSpawnStatus::Dead, 0},
		});
		if (store.Size() != 2) return false;
		store.Clear();
		if (store.Size() != 0) return false;
		if (!store.LoadAll().empty()) return false;
		LOG_INFO(Core, "[PoolPersistenceTests] clear OK");
		return true;
	}
}

int main()
{
	bool ok = true;
	ok &= TestEmptyStore();
	ok &= TestSaveLoadRoundtrip();
	ok &= TestUpsertOverwrite();
	ok &= TestLoadByPool();
	ok &= TestClear();

	if (ok)
		LOG_INFO(Core, "[PoolPersistenceTests] All tests passed");
	else
		LOG_ERROR(Core, "[PoolPersistenceTests] FAIL");
	return ok ? 0 : 1;
}
