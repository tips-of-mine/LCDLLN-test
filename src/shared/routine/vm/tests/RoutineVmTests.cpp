// M101.2 — Tests de la VM zone_event (exécution + déterminisme), headless.
// Harnais maison : main() retourne le nombre d'assertions échouées (0 = OK).

#include "src/shared/routine/RoutineGraph.h"
#include "src/shared/routine/vm/MockRoutineHost.h"
#include "src/shared/routine/vm/ZoneEventVm.h"

#include <cstdio>
#include <string>

using namespace engine::routine;
using namespace engine::routine::vm;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	RoutinePin Pin(uint32_t id, PinDirection dir, PinKind kind, const char* name,
	               RoutineDataType dt = RoutineDataType::None)
	{
		RoutinePin p; p.id = id; p.direction = dir; p.kind = kind; p.dataType = dt; p.name = name;
		return p;
	}

	RoutineGraph MakeOpenDoorGraph()
	{
		RoutineGraph g;
		g.kind = RoutineGraphKind::ZoneEvent;

		RoutineNode ev; ev.id = 1; ev.type = RoutineNodeType::EventOnInteract;
		ev.pins.push_back(Pin(10, PinDirection::Output, PinKind::Exec, "fired"));

		RoutineNode act; act.id = 2; act.type = RoutineNodeType::ActionOpenInteractive;
		act.pins.push_back(Pin(20, PinDirection::Input, PinKind::Exec, "in"));
		act.pins.push_back(Pin(21, PinDirection::Output, PinKind::Exec, "out"));
		{
			RoutineProperty pid; pid.key = "interactiveId"; pid.type = RoutineDataType::EntityRef; pid.sValue = "42";
			RoutineProperty op; op.key = "open"; op.type = RoutineDataType::Bool; op.bValue = true;
			act.properties.push_back(pid);
			act.properties.push_back(op);
		}

		g.nodes.push_back(ev);
		g.nodes.push_back(act);

		RoutineLink l; l.id = 100; l.fromNodeId = 1; l.fromPinId = 10; l.toNodeId = 2; l.toPinId = 20;
		g.links.push_back(l);
		return g;
	}

	RoutineGraph MakeBranchGraph()
	{
		RoutineGraph g;
		g.kind = RoutineGraphKind::ZoneEvent;

		RoutineNode ev; ev.id = 1; ev.type = RoutineNodeType::EventOnInteract;
		ev.pins.push_back(Pin(10, PinDirection::Output, PinKind::Exec, "fired"));

		RoutineNode br; br.id = 2; br.type = RoutineNodeType::BranchIf;
		br.pins.push_back(Pin(20, PinDirection::Input, PinKind::Exec, "in"));
		br.pins.push_back(Pin(21, PinDirection::Output, PinKind::Exec, "true"));
		br.pins.push_back(Pin(22, PinDirection::Output, PinKind::Exec, "false"));
		br.pins.push_back(Pin(23, PinDirection::Input, PinKind::Data, "cond", RoutineDataType::Bool));

		RoutineNode season; season.id = 3; season.type = RoutineNodeType::ActionBroadcastSeason;
		season.pins.push_back(Pin(30, PinDirection::Input, PinKind::Exec, "in"));
		season.pins.push_back(Pin(31, PinDirection::Output, PinKind::Exec, "out"));
		{ RoutineProperty p; p.key = "seasonIndex"; p.type = RoutineDataType::Int; p.iValue = 1; season.properties.push_back(p); }

		RoutineNode weather; weather.id = 4; weather.type = RoutineNodeType::ActionBroadcastWeather;
		weather.pins.push_back(Pin(40, PinDirection::Input, PinKind::Exec, "in"));
		weather.pins.push_back(Pin(41, PinDirection::Output, PinKind::Exec, "out"));
		{ RoutineProperty p; p.key = "weatherIndex"; p.type = RoutineDataType::Int; p.iValue = 3; weather.properties.push_back(p); }

		g.nodes = { ev, br, season, weather };
		g.links = {
			RoutineLink{ 100, 1, 10, 2, 20 },  // event -> branch
			RoutineLink{ 101, 2, 21, 3, 30 },  // true  -> season
			RoutineLink{ 102, 2, 22, 4, 40 },  // false -> weather
		};
		return g;
	}

	void Test_OpenDoorChain()
	{
		RoutineGraph g = MakeOpenDoorGraph();
		ZoneEventVm vm(g);
		MockRoutineHost host;
		RoutineRunContext ctx;
		bool fired = vm.Fire(RoutineNodeType::EventOnInteract, ctx, host);
		REQUIRE(fired);
		REQUIRE(host.openCalls.size() == 1);
		if (!host.openCalls.empty())
		{
			REQUIRE(host.openCalls[0].first == 42u);
			REQUIRE(host.openCalls[0].second == true);
		}
	}

	void Test_BranchDefaultFalse()
	{
		RoutineGraph g = MakeBranchGraph();
		ZoneEventVm vm(g);
		MockRoutineHost host; // cond non lié → false → branche "false" → weather
		RoutineRunContext ctx;
		REQUIRE(vm.Fire(RoutineNodeType::EventOnInteract, ctx, host));
		REQUIRE(host.weatherCalls.size() == 1);
		REQUIRE(host.seasonCalls.empty());
		if (!host.weatherCalls.empty()) REQUIRE(host.weatherCalls[0] == 3);
	}

	void Test_Determinism_SameTrace()
	{
		RoutineGraph g = MakeOpenDoorGraph();
		ZoneEventVm vm(g);
		MockRoutineHost a, b;
		RoutineRunContext ctx;
		vm.Fire(RoutineNodeType::EventOnInteract, ctx, a);
		vm.Fire(RoutineNodeType::EventOnInteract, ctx, b);
		REQUIRE(a.trace == b.trace);
		REQUIRE(!a.trace.empty());
	}

	void Test_WrongKindOrMissingEvent()
	{
		RoutineGraph g = MakeOpenDoorGraph();
		ZoneEventVm vm(g);
		MockRoutineHost host;
		RoutineRunContext ctx;
		// Type d'event absent du graphe.
		REQUIRE(!vm.Fire(RoutineNodeType::EventOnZoneExit, ctx, host));

		// Mauvais kind.
		RoutineGraph npc; npc.kind = RoutineGraphKind::NpcRoutine;
		ZoneEventVm vm2(npc);
		REQUIRE(!vm2.Fire(RoutineNodeType::EventOnInteract, ctx, host));
	}
}

int main()
{
	Test_OpenDoorChain();
	Test_BranchDefaultFalse();
	Test_Determinism_SameTrace();
	Test_WrongKindOrMissingEvent();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutineVmTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutineVmTests: %d échec(s)\n", g_failed);
	return g_failed;
}
