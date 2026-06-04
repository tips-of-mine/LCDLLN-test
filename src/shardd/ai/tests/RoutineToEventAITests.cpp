// M101.7 (partiel) — Tests de traduction npc_routine -> EventAIRow, headless.

#include "src/shardd/ai/RoutineToEventAI.h"
#include "src/shared/routine/RoutineGraph.h"

#include <cstdio>
#include <string>

using namespace engine::routine;
using namespace engine::server::ai;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Contains(const std::string& hay, const char* needle)
	{
		return hay.find(needle) != std::string::npos;
	}

	RoutineNode TaskNode(uint32_t id, RoutineNodeType type, const char* propKey, const char* propVal)
	{
		RoutineNode n; n.id = id; n.type = type;
		if (propKey)
		{
			RoutineProperty p; p.key = propKey; p.type = RoutineDataType::String; p.sValue = propVal;
			n.properties.push_back(p);
		}
		return n;
	}

	void Test_MappableTasks()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::NpcRoutine;
		g.nodes.push_back(TaskNode(1, RoutineNodeType::NpcStateRoot, nullptr, nullptr));
		g.nodes.push_back(TaskNode(2, RoutineNodeType::TaskPlayAnim, "animId", "idle_01"));
		g.nodes.push_back(TaskNode(3, RoutineNodeType::TaskSetEmotion, "emotion", "happy"));

		std::string warn;
		auto rows = RoutineToEventAI(g, warn);
		REQUIRE(rows.size() == 2);
		REQUIRE(warn.empty());
		if (rows.size() == 2)
		{
			REQUIRE(rows[0].action == EventAction::Custom);
			REQUIRE(Contains(rows[0].actionString, "idle_01"));
			REQUIRE(rows[1].trigger == EventTrigger::OnSpawn);
			REQUIRE(Contains(rows[1].actionString, "happy"));
		}
	}

	void Test_BlockedNodesWarn()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::NpcRoutine;
		g.nodes.push_back(TaskNode(1, RoutineNodeType::TaskMoveTo, "targetRef", "post_garde"));
		g.nodes.push_back(TaskNode(2, RoutineNodeType::SensorPlayerInRange, nullptr, nullptr));

		std::string warn;
		auto rows = RoutineToEventAI(g, warn);
		REQUIRE(rows.empty());
		REQUIRE(Contains(warn, "TaskMoveTo"));
		REQUIRE(Contains(warn, "SensorPlayerInRange"));
	}

	void Test_WrongKind()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::ZoneEvent;
		std::string warn;
		auto rows = RoutineToEventAI(g, warn);
		REQUIRE(rows.empty());
		REQUIRE(!warn.empty());
	}

	void Test_Deterministic()
	{
		RoutineGraph g; g.kind = RoutineGraphKind::NpcRoutine;
		g.nodes.push_back(TaskNode(3, RoutineNodeType::TaskPlayAnim, "animId", "a"));
		g.nodes.push_back(TaskNode(1, RoutineNodeType::TaskSetEmotion, "emotion", "e"));
		std::string w1, w2;
		auto a = RoutineToEventAI(g, w1);
		auto b = RoutineToEventAI(g, w2);
		REQUIRE(a.size() == b.size());
		for (size_t i = 0; i < a.size() && i < b.size(); ++i)
			REQUIRE(a[i].eventId == b[i].eventId && a[i].actionString == b[i].actionString);
	}
}

int main()
{
	Test_MappableTasks();
	Test_BlockedNodesWarn();
	Test_WrongKind();
	Test_Deterministic();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutineToEventAITests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutineToEventAITests: %d échec(s)\n", g_failed);
	return g_failed;
}
