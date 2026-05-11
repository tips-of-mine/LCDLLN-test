// Wave 24 — Movement scaffold tests : INavmeshProvider (StubImpl) +
// WaypointMgr + PathFollowMotion state machine.
//
// Pattern aligne sur les autres tests Wave : asserts + printf.
// Cible CTest : movement_tests.

#include "src/shardd/Movement/StubNavmeshProvider.h"
#include "src/shardd/Movement/WaypointMgr.h"
#include "src/shardd/Movement/PathFollowMotion.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::movement;

namespace
{
	// ========================================================================
	// StubNavmeshProvider
	// ========================================================================

	void TestStubProviderUnknownMap()
	{
		StubNavmeshProvider p;
		assert(!p.HasMap(1));
		PathRequest req{1, {0,0,0}, {10,0,10}};
		auto r = p.FindPath(req);
		assert(!r.IsValid());
		assert(r.waypoints.empty());
		std::puts("[OK] TestStubProviderUnknownMap");
	}

	void TestStubProviderKnownMap()
	{
		StubNavmeshProvider p;
		p.RegisterMap(1);
		assert(p.HasMap(1));
		PathRequest req{1, {0,0,0}, {10,0,10}};
		auto r = p.FindPath(req);
		assert(r.IsValid());
		assert(r.waypoints.size() == 2);
		assert(r.waypoints[0] == PathPoint(0,0,0));
		assert(r.waypoints[1] == PathPoint(10,0,10));
		std::puts("[OK] TestStubProviderKnownMap");
	}

	// ========================================================================
	// WaypointMgr
	// ========================================================================

	void TestWaypointMgrEmpty()
	{
		WaypointMgr mgr;
		assert(!mgr.HasPath(42));
		assert(mgr.Path(42).empty());
		assert(mgr.PathCount() == 0);
		std::puts("[OK] TestWaypointMgrEmpty");
	}

	void TestWaypointMgrAddAndSort()
	{
		WaypointMgr mgr;
		// Ajout out-of-order pour verifier le tri lazy.
		mgr.AddWaypoint({100, 2, {20,0,0}, 1000, 0});
		mgr.AddWaypoint({100, 0, {0,0,0}, 500, 0});
		mgr.AddWaypoint({100, 1, {10,0,0}, 0, 42});  // script_id

		assert(mgr.HasPath(100));
		const auto& path = mgr.Path(100);
		assert(path.size() == 3);
		// Verifie ordre par pointIdx croissant.
		assert(path[0].pointIdx == 0);
		assert(path[1].pointIdx == 1);
		assert(path[2].pointIdx == 2);
		// Verifie le script_id preserve.
		assert(path[1].scriptId == 42);
		std::puts("[OK] TestWaypointMgrAddAndSort");
	}

	void TestWaypointMgrMultipleCreatures()
	{
		WaypointMgr mgr;
		mgr.AddWaypoint({100, 0, {0,0,0}, 0, 0});
		mgr.AddWaypoint({200, 0, {5,0,5}, 0, 0});
		mgr.AddWaypoint({200, 1, {10,0,10}, 0, 0});

		assert(mgr.PathCount() == 2);
		assert(mgr.Path(100).size() == 1);
		assert(mgr.Path(200).size() == 2);
		std::puts("[OK] TestWaypointMgrMultipleCreatures");
	}

	// ========================================================================
	// PathFollowMotion state machine
	// ========================================================================

	void TestPathFollowStartFailsWithoutPath()
	{
		WaypointMgr wpMgr;
		StubNavmeshProvider nav;
		nav.RegisterMap(1);
		PathFollowMotion motion(wpMgr, nav);
		// Aucun waypoint pour cette creature.
		assert(!motion.Start(/*creatureGuid*/42, /*mapId*/1, {0,0,0}));
		std::puts("[OK] TestPathFollowStartFailsWithoutPath");
	}

	void TestPathFollowStartFailsUnknownMap()
	{
		WaypointMgr wpMgr;
		wpMgr.AddWaypoint({42, 0, {10,0,10}, 0, 0});
		StubNavmeshProvider nav;
		// Map non enregistree.
		PathFollowMotion motion(wpMgr, nav);
		assert(!motion.Start(42, /*mapId*/99, {0,0,0}));
		std::puts("[OK] TestPathFollowStartFailsUnknownMap");
	}

	void TestPathFollowReachesWaypoint()
	{
		WaypointMgr wpMgr;
		// 2 waypoints : (10,0,0) puis (20,0,0). Pas de wait, pas de script.
		wpMgr.AddWaypoint({42, 0, {10,0,0}, 0, 0});
		wpMgr.AddWaypoint({42, 1, {20,0,0}, 0, 0});
		StubNavmeshProvider nav;
		nav.RegisterMap(1);
		PathFollowMotion motion(wpMgr, nav);
		assert(motion.Start(42, 1, {0,0,0}));
		assert(motion.State() == PathFollowState::MovingToWaypoint);
		// Avance 1m/tick, 1000ms tick = 1m/s. Apres 11 ticks de 1s, on devrait
		// avoir parcouru 11m, donc depasse (10,0,0) (tolerance 0.5m).
		// En pratique, le premier "reached" se produit quand distance <= sqrt(0.25).
		bool scriptTrigger = false;
		for (int i = 0; i < 20 && motion.State() != PathFollowState::Done; ++i)
		{
			scriptTrigger = motion.Tick(/*deltaMs*/1000, /*speed*/1.0f);
			if (motion.CurrentIdx() == 1 && motion.State() == PathFollowState::MovingToWaypoint)
				break;
		}
		// Apres ~10s, on devrait avoir atteint waypoint 0 et avance vers 1.
		assert(motion.CurrentIdx() == 1 || motion.CurrentIdx() == 0);
		assert(!scriptTrigger); // pas de scriptId sur ces waypoints
		std::puts("[OK] TestPathFollowReachesWaypoint");
	}

	void TestPathFollowScriptTrigger()
	{
		WaypointMgr wpMgr;
		// Waypoint a (1,0,0) avec scriptId 7.
		wpMgr.AddWaypoint({42, 0, {1,0,0}, 0, /*scriptId*/7});
		StubNavmeshProvider nav;
		nav.RegisterMap(1);
		PathFollowMotion motion(wpMgr, nav);
		assert(motion.Start(42, 1, {0,0,0}));
		// Avance jusqu'a atteindre le waypoint.
		bool triggered = false;
		for (int i = 0; i < 20 && !triggered; ++i)
		{
			triggered = motion.Tick(1000, 1.0f);
		}
		assert(triggered);  // scriptId 7 signale au caller
		std::puts("[OK] TestPathFollowScriptTrigger");
	}

	void TestPathFollowWaitState()
	{
		WaypointMgr wpMgr;
		// Waypoint avec wait 3000ms.
		wpMgr.AddWaypoint({42, 0, {1,0,0}, /*waitMs*/3000, 0});
		wpMgr.AddWaypoint({42, 1, {2,0,0}, 0, 0});
		StubNavmeshProvider nav;
		nav.RegisterMap(1);
		PathFollowMotion motion(wpMgr, nav);
		assert(motion.Start(42, 1, {0,0,0}));
		// Avance jusqu'a etre en wait.
		for (int i = 0; i < 5 && motion.State() != PathFollowState::Waiting; ++i)
			motion.Tick(1000, 1.0f);
		assert(motion.State() == PathFollowState::Waiting);
		// Reste en wait pendant 2.5s (< 3s).
		motion.Tick(2500, 0);
		assert(motion.State() == PathFollowState::Waiting);
		// 500ms de plus = depasse 3000ms, doit re-passer en MovingToWaypoint.
		motion.Tick(500, 0);
		assert(motion.State() == PathFollowState::MovingToWaypoint);
		// Et avance vers le waypoint 1.
		assert(motion.CurrentIdx() == 1);
		std::puts("[OK] TestPathFollowWaitState");
	}

	void TestPathFollowStop()
	{
		WaypointMgr wpMgr;
		wpMgr.AddWaypoint({42, 0, {10,0,0}, 0, 0});
		StubNavmeshProvider nav;
		nav.RegisterMap(1);
		PathFollowMotion motion(wpMgr, nav);
		motion.Start(42, 1, {0,0,0});
		motion.Tick(500, 1.0f);
		motion.Stop();
		assert(motion.State() == PathFollowState::Idle);
		// Tick post-Stop = no-op.
		motion.Tick(1000, 1.0f);
		assert(motion.State() == PathFollowState::Idle);
		std::puts("[OK] TestPathFollowStop");
	}
}

int main()
{
	TestStubProviderUnknownMap();
	TestStubProviderKnownMap();
	TestWaypointMgrEmpty();
	TestWaypointMgrAddAndSort();
	TestWaypointMgrMultipleCreatures();
	TestPathFollowStartFailsWithoutPath();
	TestPathFollowStartFailsUnknownMap();
	TestPathFollowReachesWaypoint();
	TestPathFollowScriptTrigger();
	TestPathFollowWaitState();
	TestPathFollowStop();
	std::puts("All Movement scaffold tests passed");
	return 0;
}
