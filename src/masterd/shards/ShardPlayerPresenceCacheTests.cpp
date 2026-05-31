/**
 * Tests unitaires — ShardPlayerPresenceCache (Niveau 2, présence agrégée master).
 * Pur (pas de réseau/DB). Retourne 0 si OK, non-zéro sinon.
 *
 * Couvre : Update remplace l'ensemble d'un shard, isolation entre shards,
 * Clear par shard, Get, Snapshot, et l'omission des accountId == 0.
 */

#include "src/masterd/shards/ShardPlayerPresenceCache.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

using engine::server::ShardPlayerPresenceCache;
using engine::network::ShardPlayerPresence;

namespace
{
	int s_failCount = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	// Helper : construit une entrée de présence shard.
	ShardPlayerPresence P(uint64_t acc, uint64_t chr, uint32_t lvl, uint32_t zone)
	{
		ShardPlayerPresence p;
		p.accountId = acc;
		p.characterId = chr;
		p.level = lvl;
		p.zoneId = zone;
		return p;
	}
}

static void TestUpdateAndGet()
{
	ShardPlayerPresenceCache cache;
	Assert(!cache.Get(1).has_value(), "Get inconnu => nullopt");

	cache.Update(10u, { P(1, 100, 7, 42), P(2, 200, 1, 0) });
	auto e1 = cache.Get(1);
	Assert(e1.has_value(), "Get(1) après Update");
	if (e1)
	{
		Assert(e1->characterId == 100 && e1->level == 7 && e1->zoneId == 42, "champs round-trip");
		Assert(e1->shardId == 10u, "shardId renseigné par Update");
	}
	Assert(cache.Snapshot().size() == 2u, "Snapshot == 2");
}

static void TestUpdateReplacesShardSet()
{
	ShardPlayerPresenceCache cache;
	cache.Update(10u, { P(1, 100, 7, 42), P(2, 200, 1, 0) });
	// Le compte 2 a quitté le jeu -> nouveau rapport sans lui.
	cache.Update(10u, { P(1, 100, 8, 43) });
	Assert(cache.Get(1).has_value(), "compte 1 toujours présent");
	Assert(!cache.Get(2).has_value(), "compte 2 retiré (absent du nouveau rapport)");
	auto e1 = cache.Get(1);
	Assert(e1 && e1->level == 8 && e1->zoneId == 43, "compte 1 mis à jour (level/zone)");
	Assert(cache.Snapshot().size() == 1u, "Snapshot == 1 après remplacement");
}

static void TestShardIsolationAndClear()
{
	ShardPlayerPresenceCache cache;
	cache.Update(10u, { P(1, 100, 7, 42) });
	cache.Update(20u, { P(2, 200, 5, 7) });
	Assert(cache.Snapshot().size() == 2u, "deux shards => 2 entrées");

	// Update du shard 10 ne touche pas les entrées du shard 20.
	cache.Update(10u, { P(1, 100, 9, 50) });
	Assert(cache.Get(2).has_value(), "entrée shard 20 intacte après update shard 10");

	// Clear ne purge que le shard ciblé.
	cache.Clear(10u);
	Assert(!cache.Get(1).has_value(), "Clear(10) retire le compte 1");
	Assert(cache.Get(2).has_value(), "Clear(10) ne touche pas le shard 20");
	Assert(cache.Snapshot().size() == 1u, "Snapshot == 1 après Clear(10)");
}

static void TestAccountZeroIgnored()
{
	ShardPlayerPresenceCache cache;
	cache.Update(10u, { P(0, 100, 7, 42), P(3, 300, 2, 9) });
	Assert(!cache.Get(0).has_value(), "accountId 0 ignoré");
	Assert(cache.Get(3).has_value(), "compte valide conservé");
	Assert(cache.Snapshot().size() == 1u, "seule l'entrée valide est gardée");
}

int main()
{
	TestUpdateAndGet();
	TestUpdateReplacesShardSet();
	TestShardIsolationAndClear();
	TestAccountZeroIgnored();
	std::cerr << (s_failCount == 0 ? "[OK] all shard_player_presence_cache tests passed\n" : "[FAIL] some tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
