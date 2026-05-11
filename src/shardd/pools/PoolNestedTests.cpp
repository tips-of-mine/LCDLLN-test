// Wave 20 — Pool nested tests : pools imbriques (pool_pool) + cycle
// detection. Le PoolManager existant reste backward compatible : si
// `nested` est vide, comportement identique a la Phase 4.22a.

#include "src/shardd/pools/PoolManager.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <random>
#include <unordered_set>

namespace
{
	using engine::server::pools::NestedPoolEntry;
	using engine::server::pools::Pool;
	using engine::server::pools::PoolEntry;
	using engine::server::pools::PoolManager;
	using engine::server::pools::SpawnId;

	/// Backward compat : pool sans nested -> comportement Phase 4.22a.
	bool TestBackwardCompatNoNested()
	{
		PoolManager mgr;
		Pool p;
		p.poolId = 1;
		p.maxActive = 1;
		p.entries = {{100, 1.0f}, {200, 1.0f}, {300, 1.0f}};
		// p.nested est vide par defaut.
		mgr.Register(p);
		std::mt19937 rng(42);
		auto out = mgr.Roll(1, rng);
		if (out.size() != 1) return false;
		if (out[0] != 100 && out[0] != 200 && out[0] != 300) return false;
		LOG_INFO(Core, "[PoolNestedTests] backward compat no-nested OK");
		return true;
	}

	/// Pool parent avec uniquement une nested entry -> recurse dans le
	/// child et retourne ses spawns. Aucun spawn direct dans le parent.
	bool TestSingleNestedRecurse()
	{
		PoolManager mgr;
		Pool child;
		child.poolId = 10;
		child.maxActive = 2;
		child.entries = {{500, 1.0f}, {501, 1.0f}};
		mgr.Register(child);

		Pool parent;
		parent.poolId = 1;
		parent.maxActive = 1;  // 1 entry pickee dans le parent (qui est la nested)
		parent.nested = {{10, 1.0f}};
		mgr.Register(parent);

		std::mt19937 rng(42);
		auto out = mgr.Roll(1, rng);
		// Le parent pick l'entry nested -> recurse child (maxActive=2) -> 2 spawns.
		if (out.size() != 2) return false;
		// Les spawns doivent etre 500 et 501.
		std::sort(out.begin(), out.end());
		if (out[0] != 500 || out[1] != 501) return false;
		LOG_INFO(Core, "[PoolNestedTests] single nested recurse OK");
		return true;
	}

	/// Mix entries directs + nested. Avec maxActive=1, on pick UN seul
	/// candidat (spawn direct OU nested) -> output 1 ou plusieurs spawns
	/// selon le tirage.
	bool TestMixDirectAndNested()
	{
		PoolManager mgr;
		Pool child;
		child.poolId = 10;
		child.maxActive = 3;  // si nested pickee, recurse fournit 3 spawns
		child.entries = {{500, 1.0f}, {501, 1.0f}, {502, 1.0f}};
		mgr.Register(child);

		Pool parent;
		parent.poolId = 1;
		parent.maxActive = 1;
		parent.entries = {{100, 1.0f}};
		parent.nested = {{10, 1.0f}};
		mgr.Register(parent);

		std::mt19937 rng(7);
		auto out = mgr.Roll(1, rng);
		// Soit 1 spawn (100 si direct pickee) soit 3 spawns (500/501/502
		// si nested pickee).
		if (out.size() != 1 && out.size() != 3) return false;
		LOG_INFO(Core, "[PoolNestedTests] mix direct/nested OK");
		return true;
	}

	/// Cycle simple A -> B -> A : detection via visited set, skip silent.
	/// Le parent retourne juste ses spawns directs s'il en a, ou vide.
	bool TestCycleDetection2Pools()
	{
		PoolManager mgr;
		Pool a;
		a.poolId = 1;
		a.maxActive = 1;
		a.entries = {{100, 1.0f}};
		a.nested = {{2, 1.0f}};
		mgr.Register(a);

		Pool b;
		b.poolId = 2;
		b.maxActive = 1;
		b.entries = {{200, 1.0f}};
		b.nested = {{1, 1.0f}};  // cycle vers A
		mgr.Register(b);

		// Avec ce setup et n'importe quel rng, on ne doit JAMAIS crasher
		// ni boucler infiniment. Le resultat doit etre un sous-ensemble
		// de {100, 200}.
		std::mt19937 rng(99);
		auto out = mgr.Roll(1, rng);
		// Verification : pas de crash, et tous les spawns retournes sont
		// dans {100, 200}.
		for (auto s : out)
		{
			if (s != 100 && s != 200) return false;
		}
		LOG_INFO(Core, "[PoolNestedTests] cycle 2 pools OK (size={})", out.size());
		return true;
	}

	/// Cycle 3 pools : A -> B -> C -> A. Meme garantie : pas de crash,
	/// resultat dans {100, 200, 300}.
	bool TestCycleDetection3Pools()
	{
		PoolManager mgr;
		Pool a; a.poolId = 1; a.maxActive = 1; a.entries = {{100, 1.0f}}; a.nested = {{2, 1.0f}}; mgr.Register(a);
		Pool b; b.poolId = 2; b.maxActive = 1; b.entries = {{200, 1.0f}}; b.nested = {{3, 1.0f}}; mgr.Register(b);
		Pool c; c.poolId = 3; c.maxActive = 1; c.entries = {{300, 1.0f}}; c.nested = {{1, 1.0f}}; mgr.Register(c);

		std::mt19937 rng(123);
		auto out = mgr.Roll(1, rng);
		for (auto s : out)
		{
			if (s != 100 && s != 200 && s != 300) return false;
		}
		LOG_INFO(Core, "[PoolNestedTests] cycle 3 pools OK (size={})", out.size());
		return true;
	}

	/// Nested pointe vers un pool inexistant -> skip silent, parent
	/// retourne juste ses spawns directs.
	bool TestNestedUnknownPool()
	{
		PoolManager mgr;
		Pool a;
		a.poolId = 1;
		a.maxActive = 2;  // pick 2 candidats sur 2 disponibles
		a.entries = {{100, 1.0f}};
		a.nested = {{999, 1.0f}};  // pool 999 n'existe pas
		mgr.Register(a);

		std::mt19937 rng(42);
		auto out = mgr.Roll(1, rng);
		// Spawn 100 obligatoirement dans le resultat (l'autre candidat
		// est nested vers 999 qui est skip silent).
		// Resultat : soit [100] (si nested pickee 1er + 100 pickee 2e) soit [100]
		// (si 100 pickee 1er, nested pickee 2e mais 999 inexistant).
		bool has100 = std::find(out.begin(), out.end(), 100) != out.end();
		if (!has100) return false;
		LOG_INFO(Core, "[PoolNestedTests] nested unknown pool OK");
		return true;
	}

	/// Pool a sans entries ni nested -> retourne vide.
	bool TestEmptyPoolBothFields()
	{
		PoolManager mgr;
		Pool a;
		a.poolId = 1;
		a.maxActive = 5;
		// entries et nested vides
		mgr.Register(a);

		std::mt19937 rng(1);
		auto out = mgr.Roll(1, rng);
		if (!out.empty()) return false;
		LOG_INFO(Core, "[PoolNestedTests] empty both fields OK");
		return true;
	}
}

int main()
{
	bool ok = true;
	ok &= TestBackwardCompatNoNested();
	ok &= TestSingleNestedRecurse();
	ok &= TestMixDirectAndNested();
	ok &= TestCycleDetection2Pools();
	ok &= TestCycleDetection3Pools();
	ok &= TestNestedUnknownPool();
	ok &= TestEmptyPoolBothFields();

	if (ok)
		LOG_INFO(Core, "[PoolNestedTests] All tests passed");
	else
		LOG_ERROR(Core, "[PoolNestedTests] FAIL");
	return ok ? 0 : 1;
}
