// Wave 19 — HostileRefManager tests : pattern bidirectionnel cibles
// (m_targets) + attaquants (m_haters), avec helpers EngageHostile /
// DisengageHostile / CleanupOnDeath pour la synchronisation symetrique.
//
// Pattern aligne sur les autres tests entities/combat : asserts +
// printf, pas de framework. Cible CTest : hostile_ref_manager_tests.

#include "src/shardd/combat/HostileRefManager.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace engine::server::combat;
using engine::server::EntityId;

namespace
{
	/// Construction : 2 sets vides, IsEngaged false.
	void TestHostileRefManagerEmpty()
	{
		HostileRefManager mgr;
		assert(mgr.TargetCount() == 0);
		assert(mgr.HaterCount() == 0);
		assert(!mgr.IsEngaged());
		assert(!mgr.HasTarget(42));
		assert(!mgr.HasHater(42));
		std::puts("[OK] TestHostileRefManagerEmpty");
	}

	/// AddTarget : 1 cible dans m_targets, m_haters vide, IsEngaged true.
	void TestAddTarget()
	{
		HostileRefManager mgr;
		mgr.AddTarget(100);
		assert(mgr.TargetCount() == 1);
		assert(mgr.HasTarget(100));
		assert(mgr.HaterCount() == 0);
		assert(mgr.IsEngaged());
		std::puts("[OK] TestAddTarget");
	}

	/// AddHater : 1 attaquant dans m_haters, m_targets vide, IsEngaged true.
	void TestAddHater()
	{
		HostileRefManager mgr;
		mgr.AddHater(200);
		assert(mgr.HaterCount() == 1);
		assert(mgr.HasHater(200));
		assert(mgr.TargetCount() == 0);
		assert(mgr.IsEngaged());
		std::puts("[OK] TestAddHater");
	}

	/// AddTarget idempotent : ajouter 2 fois le meme id -> 1 entry.
	void TestAddTargetIdempotent()
	{
		HostileRefManager mgr;
		mgr.AddTarget(50);
		mgr.AddTarget(50);
		mgr.AddTarget(50);
		assert(mgr.TargetCount() == 1);
		std::puts("[OK] TestAddTargetIdempotent");
	}

	/// RemoveTarget : retire bien la cible. No-op sur id inconnu.
	void TestRemoveTarget()
	{
		HostileRefManager mgr;
		mgr.AddTarget(100);
		mgr.AddTarget(200);
		mgr.RemoveTarget(100);
		assert(mgr.TargetCount() == 1);
		assert(!mgr.HasTarget(100));
		assert(mgr.HasTarget(200));
		// No-op sur id inconnu.
		mgr.RemoveTarget(999);
		assert(mgr.TargetCount() == 1);
		std::puts("[OK] TestRemoveTarget");
	}

	/// Clear : vide les 2 listes, IsEngaged false.
	void TestClear()
	{
		HostileRefManager mgr;
		mgr.AddTarget(1);
		mgr.AddTarget(2);
		mgr.AddHater(10);
		mgr.AddHater(20);
		assert(mgr.IsEngaged());
		mgr.Clear();
		assert(mgr.TargetCount() == 0);
		assert(mgr.HaterCount() == 0);
		assert(!mgr.IsEngaged());
		std::puts("[OK] TestClear");
	}

	/// EngageHostile : synchronisation automatique des 2 cotes A -> B.
	/// A.targets contient B, B.haters contient A.
	void TestEngageHostileSynchronization()
	{
		HostileRefManager a;
		HostileRefManager b;
		const EntityId idA = 1;
		const EntityId idB = 2;

		EngageHostile(a, idA, b, idB);

		assert(a.HasTarget(idB));
		assert(!a.HasHater(idB));
		assert(b.HasHater(idA));
		assert(!b.HasTarget(idA));
		std::puts("[OK] TestEngageHostileSynchronization");
	}

	/// DisengageHostile : retire la paire des 2 cotes (inverse exact d'Engage).
	void TestDisengageHostileSynchronization()
	{
		HostileRefManager a;
		HostileRefManager b;
		const EntityId idA = 1;
		const EntityId idB = 2;

		EngageHostile(a, idA, b, idB);
		assert(a.HasTarget(idB) && b.HasHater(idA));

		DisengageHostile(a, idA, b, idB);
		assert(!a.HasTarget(idB));
		assert(!b.HasHater(idA));
		assert(a.TargetCount() == 0);
		assert(b.HaterCount() == 0);
		std::puts("[OK] TestDisengageHostileSynchronization");
	}

	/// CleanupOnDeath : retire deadId de tous les managers fournis,
	/// cote targets ET cote haters. Idempotent et sur sur nullptr.
	void TestCleanupOnDeathRemovesFromAllManagers()
	{
		HostileRefManager mgrA;  // hait deadId
		HostileRefManager mgrB;  // est hai par deadId
		HostileRefManager mgrC;  // pas de relation avec deadId
		const EntityId deadId = 666;

		// A hait deadId
		mgrA.AddTarget(deadId);
		mgrA.AddTarget(100);  // autre cible, ne doit pas etre touchee
		// B est hai par deadId
		mgrB.AddHater(deadId);
		mgrB.AddHater(200);  // autre hater, ne doit pas etre touche
		// C n'a aucune relation avec deadId, mais a d'autres relations
		mgrC.AddTarget(300);
		mgrC.AddHater(400);

		std::vector<HostileRefManager*> managers = {&mgrA, &mgrB, &mgrC, nullptr};
		CleanupOnDeath(deadId, managers);

		// deadId est retire partout
		assert(!mgrA.HasTarget(deadId));
		assert(!mgrB.HasHater(deadId));

		// Les autres relations sont preservees
		assert(mgrA.HasTarget(100));
		assert(mgrB.HasHater(200));
		assert(mgrC.HasTarget(300));
		assert(mgrC.HasHater(400));

		// Idempotent : second appel ne casse rien
		CleanupOnDeath(deadId, managers);
		assert(mgrA.TargetCount() == 1);
		assert(mgrB.HaterCount() == 1);
		std::puts("[OK] TestCleanupOnDeathRemovesFromAllManagers");
	}

	/// Scenario realiste : 1 player vs 3 mobs. Le player tue mob1. Verifier
	/// que mob1 n'est plus dans les m_haters du player, et que le player
	/// n'est plus dans les m_targets de mob1 (mais mob2/mob3 conservent
	/// le player en target).
	void TestRealisticCombatScenario()
	{
		HostileRefManager player;
		HostileRefManager mob1, mob2, mob3;
		const EntityId pId = 1;
		const EntityId m1 = 101, m2 = 102, m3 = 103;

		// Les 3 mobs aggro le player
		EngageHostile(mob1, m1, player, pId);
		EngageHostile(mob2, m2, player, pId);
		EngageHostile(mob3, m3, player, pId);

		assert(player.HaterCount() == 3);
		assert(player.HasHater(m1) && player.HasHater(m2) && player.HasHater(m3));
		assert(mob1.HasTarget(pId));
		assert(mob2.HasTarget(pId));
		assert(mob3.HasTarget(pId));

		// Player retourne le combat sur mob1 (cible offensive principale)
		EngageHostile(player, pId, mob1, m1);
		assert(player.HasTarget(m1));
		assert(mob1.HasHater(pId));

		// Player tue mob1 -> cleanup global
		std::vector<HostileRefManager*> all = {&player, &mob1, &mob2, &mob3};
		CleanupOnDeath(m1, all);

		// mob1 disparait des relations
		assert(!player.HasTarget(m1));  // plus de cible sur lui
		assert(!player.HasHater(m1));   // plus d'attaquant
		// mais mob2, mob3 toujours en aggro sur le player
		assert(player.HasHater(m2));
		assert(player.HasHater(m3));
		assert(player.HaterCount() == 2);
		std::puts("[OK] TestRealisticCombatScenario");
	}
}

int main()
{
	TestHostileRefManagerEmpty();
	TestAddTarget();
	TestAddHater();
	TestAddTargetIdempotent();
	TestRemoveTarget();
	TestClear();
	TestEngageHostileSynchronization();
	TestDisengageHostileSynchronization();
	TestCleanupOnDeathRemovesFromAllManagers();
	TestRealisticCombatScenario();
	std::puts("All HostileRefManager tests passed");
	return 0;
}
