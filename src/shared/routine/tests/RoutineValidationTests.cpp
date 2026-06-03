// M101.6 — Tests de validation de graphe (une règle par cas), headless.

#include "src/shared/routine/RoutineGraph.h"
#include "src/shared/routine/RoutineGraphValidation.h"

#include <cstdio>

using namespace engine::routine;
using namespace engine::routine::validation;

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

	bool HasKind(const std::vector<ValidationIssue>& v, IssueKind k)
	{
		for (const auto& i : v) if (i.kind == k) return true;
		return false;
	}

	RoutineNode EventNode()
	{
		RoutineNode ev; ev.id = 1; ev.type = RoutineNodeType::EventOnInteract;
		ev.pins.push_back(Pin(10, PinDirection::Output, PinKind::Exec, "fired"));
		return ev;
	}

	void Test_ValidGraph_NoErrors()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		RoutineNode ev = EventNode();
		RoutineNode act; act.id = 2; act.type = RoutineNodeType::ActionBroadcastSeason;
		act.pins.push_back(Pin(20, PinDirection::Input, PinKind::Exec, "in"));
		act.pins.push_back(Pin(21, PinDirection::Output, PinKind::Exec, "out"));
		g.nodes = { ev, act };
		g.links = { RoutineLink{ 100, 1, 10, 2, 20 } };
		auto issues = Validate(g);
		REQUIRE(!HasError(issues));
	}

	void Test_IncompatiblePins()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		RoutineNode ev = EventNode();
		RoutineNode act; act.id = 2; act.type = RoutineNodeType::BranchIf;
		act.pins.push_back(Pin(20, PinDirection::Input, PinKind::Data, "cond", RoutineDataType::Bool));
		g.nodes = { ev, act };
		// Lien exec (sortie) -> data (entrée) : incompatible.
		g.links = { RoutineLink{ 100, 1, 10, 2, 20 } };
		auto issues = Validate(g);
		REQUIRE(HasKind(issues, IssueKind::IncompatiblePins));
	}

	void Test_ExecCycle()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		RoutineNode ev = EventNode();
		RoutineNode a; a.id = 2; a.type = RoutineNodeType::ActionBroadcastSeason;
		a.pins.push_back(Pin(20, PinDirection::Input, PinKind::Exec, "in"));
		a.pins.push_back(Pin(21, PinDirection::Output, PinKind::Exec, "out"));
		RoutineNode b; b.id = 3; b.type = RoutineNodeType::ActionBroadcastWeather;
		b.pins.push_back(Pin(30, PinDirection::Input, PinKind::Exec, "in"));
		b.pins.push_back(Pin(31, PinDirection::Output, PinKind::Exec, "out"));
		g.nodes = { ev, a, b };
		g.links = {
			RoutineLink{ 100, 1, 10, 2, 20 }, // event -> a
			RoutineLink{ 101, 2, 21, 3, 30 }, // a -> b
			RoutineLink{ 102, 3, 31, 2, 20 }, // b -> a  (cycle)
		};
		auto issues = Validate(g);
		REQUIRE(HasKind(issues, IssueKind::ExecCycle));
	}

	void Test_OrphanNode()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		RoutineNode ev = EventNode(); // racine, pas de lien
		RoutineNode lone; lone.id = 2; lone.type = RoutineNodeType::ActionBroadcastSeason;
		lone.pins.push_back(Pin(20, PinDirection::Input, PinKind::Exec, "in"));
		g.nodes = { ev, lone };
		auto issues = Validate(g);
		REQUIRE(HasKind(issues, IssueKind::OrphanNode));
	}

	void Test_SchemaMismatch()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		RoutineNode ev = EventNode();
		RoutineNode npc; npc.id = 2; npc.type = RoutineNodeType::TaskPlayAnim; // npc dans zone
		g.nodes = { ev, npc };
		auto issues = Validate(g);
		REQUIRE(HasKind(issues, IssueKind::SchemaMismatch));
	}

	void Test_RootCardinalityZero()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		RoutineNode c; c.id = 1; c.type = RoutineNodeType::Comment;
		g.nodes = { c };
		auto issues = Validate(g);
		REQUIRE(HasKind(issues, IssueKind::RootCardinality));
	}

	void Test_Deterministic()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		RoutineNode ev = EventNode();
		RoutineNode npc; npc.id = 2; npc.type = RoutineNodeType::TaskMoveTo;
		g.nodes = { ev, npc };
		auto a = Validate(g);
		auto b = Validate(g);
		REQUIRE(a.size() == b.size());
		for (size_t i = 0; i < a.size() && i < b.size(); ++i)
		{
			REQUIRE(a[i].kind == b[i].kind);
			REQUIRE(a[i].nodeId == b[i].nodeId);
		}
	}
}

int main()
{
	Test_ValidGraph_NoErrors();
	Test_IncompatiblePins();
	Test_ExecCycle();
	Test_OrphanNode();
	Test_SchemaMismatch();
	Test_RootCardinalityZero();
	Test_Deterministic();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutineValidationTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutineValidationTests: %d échec(s)\n", g_failed);
	return g_failed;
}
