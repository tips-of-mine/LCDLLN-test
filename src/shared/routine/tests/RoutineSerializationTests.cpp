// M101.1 — Tests de sérialisation / round-trip / déterminisme.
// Harnais maison : main() retourne le nombre d'assertions échouées (0 = OK).

#include "src/shared/routine/RoutineGraph.h"
#include "src/shared/routine/RoutineNodeSchema.h"
#include "src/shared/routine/RoutineSerialization.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace engine::routine;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	// Égalité de graphes indépendante de l'ordre (tri par id avant comparaison).
	bool PinEquals(const RoutinePin& a, const RoutinePin& b)
	{
		return a.id == b.id && a.direction == b.direction && a.kind == b.kind &&
		       a.dataType == b.dataType && a.name == b.name;
	}

	bool PropEquals(const RoutineProperty& a, const RoutineProperty& b)
	{
		if (a.key != b.key || a.type != b.type) return false;
		switch (a.type)
		{
			case RoutineDataType::Bool:  return a.bValue == b.bValue;
			case RoutineDataType::Int:   return a.iValue == b.iValue;
			case RoutineDataType::Float: return a.fValue == b.fValue;
			case RoutineDataType::Vec3:
				return a.vValue.x == b.vValue.x && a.vValue.y == b.vValue.y && a.vValue.z == b.vValue.z;
			case RoutineDataType::String:
			case RoutineDataType::EntityRef: return a.sValue == b.sValue;
			default: return true;
		}
	}

	bool GraphEquals(RoutineGraph a, RoutineGraph b)
	{
		if (a.version != b.version || a.kind != b.kind || a.name != b.name) return false;
		if (a.nodes.size() != b.nodes.size() || a.links.size() != b.links.size()) return false;

		auto byNodeId = [](const RoutineNode& x, const RoutineNode& y) { return x.id < y.id; };
		auto byLinkId = [](const RoutineLink& x, const RoutineLink& y) { return x.id < y.id; };
		std::sort(a.nodes.begin(), a.nodes.end(), byNodeId);
		std::sort(b.nodes.begin(), b.nodes.end(), byNodeId);
		std::sort(a.links.begin(), a.links.end(), byLinkId);
		std::sort(b.links.begin(), b.links.end(), byLinkId);

		for (size_t i = 0; i < a.nodes.size(); ++i)
		{
			const RoutineNode& na = a.nodes[i];
			const RoutineNode& nb = b.nodes[i];
			if (na.id != nb.id || na.type != nb.type) return false;
			if (na.canvasX != nb.canvasX || na.canvasY != nb.canvasY) return false;
			if (na.pins.size() != nb.pins.size()) return false;
			if (na.properties.size() != nb.properties.size()) return false;
			for (size_t j = 0; j < na.pins.size(); ++j)
				if (!PinEquals(na.pins[j], nb.pins[j])) return false;
			for (size_t j = 0; j < na.properties.size(); ++j)
				if (!PropEquals(na.properties[j], nb.properties[j])) return false;
		}
		for (size_t i = 0; i < a.links.size(); ++i)
		{
			const RoutineLink& la = a.links[i];
			const RoutineLink& lb = b.links[i];
			if (la.id != lb.id || la.fromNodeId != lb.fromNodeId || la.fromPinId != lb.fromPinId ||
			    la.toNodeId != lb.toNodeId || la.toPinId != lb.toPinId)
				return false;
		}
		return true;
	}

	RoutineGraph MakeSampleZoneEventGraph()
	{
		RoutineGraph g;
		g.version = kRoutineGraphVersion;
		g.kind = RoutineGraphKind::ZoneEvent;
		g.name = "porte_taverne";

		RoutineNode ev;
		ev.id = 1; ev.type = RoutineNodeType::EventOnInteract;
		ev.canvasX = 120.0f; ev.canvasY = 64.0f;
		{
			RoutinePin p; p.id = 10; p.direction = PinDirection::Output; p.kind = PinKind::Exec;
			p.dataType = RoutineDataType::None; p.name = "fired";
			ev.pins.push_back(p);
		}
		{
			RoutineProperty pr; pr.key = "interactiveId"; pr.type = RoutineDataType::EntityRef;
			pr.sValue = "porte_taverne_42";
			ev.properties.push_back(pr);
		}

		RoutineNode act;
		act.id = 2; act.type = RoutineNodeType::ActionOpenInteractive;
		act.canvasX = 360.0f; act.canvasY = 64.0f;
		{
			RoutinePin pin; pin.id = 20; pin.direction = PinDirection::Input; pin.kind = PinKind::Exec;
			pin.name = "in";
			act.pins.push_back(pin);
		}
		{
			RoutineProperty pr1; pr1.key = "interactiveId"; pr1.type = RoutineDataType::EntityRef;
			pr1.sValue = "porte_taverne_42";
			RoutineProperty pr2; pr2.key = "open"; pr2.type = RoutineDataType::Bool; pr2.bValue = true;
			act.properties.push_back(pr1);
			act.properties.push_back(pr2);
		}

		g.nodes.push_back(ev);
		g.nodes.push_back(act);

		RoutineLink l;
		l.id = 100; l.fromNodeId = 1; l.fromPinId = 10; l.toNodeId = 2; l.toPinId = 20;
		g.links.push_back(l);
		return g;
	}

	void Test_RoundTrip_Identical()
	{
		RoutineGraph g = MakeSampleZoneEventGraph();
		std::string json = serialization::ToJson(g);
		serialization::ParseError err;
		auto parsed = serialization::FromJson(json, err);
		REQUIRE(parsed.has_value());
		if (parsed) REQUIRE(GraphEquals(g, *parsed));
	}

	void Test_Serialization_StableByteForByte()
	{
		RoutineGraph g = MakeSampleZoneEventGraph();
		std::string a = serialization::ToJson(g);
		std::string b = serialization::ToJson(g);
		REQUIRE(a == b);
		// Et stable après un cycle complet.
		serialization::ParseError err;
		auto parsed = serialization::FromJson(a, err);
		REQUIRE(parsed.has_value());
		if (parsed) REQUIRE(serialization::ToJson(*parsed) == a);
	}

	void Test_FloatRoundTrip()
	{
		RoutineGraph g;
		g.kind = RoutineGraphKind::NpcRoutine;
		RoutineNode n; n.id = 1; n.type = RoutineNodeType::SensorPlayerInRange;
		n.canvasX = 123.456f; n.canvasY = -987.654f;
		RoutineProperty pr; pr.key = "rangeMeters"; pr.type = RoutineDataType::Float; pr.fValue = 3.14159f;
		n.properties.push_back(pr);
		g.nodes.push_back(n);

		std::string json = serialization::ToJson(g);
		serialization::ParseError err;
		auto parsed = serialization::FromJson(json, err);
		REQUIRE(parsed.has_value());
		if (parsed)
		{
			REQUIRE(parsed->nodes.size() == 1);
			REQUIRE(parsed->nodes[0].canvasX == 123.456f);
			REQUIRE(parsed->nodes[0].canvasY == -987.654f);
			REQUIRE(parsed->nodes[0].properties.size() == 1);
			REQUIRE(parsed->nodes[0].properties[0].fValue == 3.14159f);
		}
	}

	void Test_RejectFutureVersion()
	{
		std::string json = "{\"version\":9999,\"kind\":\"zone_event\",\"name\":\"x\",\"nodes\":[],\"links\":[]}";
		serialization::ParseError err;
		auto parsed = serialization::FromJson(json, err);
		REQUIRE(!parsed.has_value());
		REQUIRE(!err.message.empty());
	}

	void Test_RejectUnknownNodeType()
	{
		std::string json =
			"{\"version\":1,\"kind\":\"zone_event\",\"name\":\"x\","
			"\"nodes\":[{\"id\":1,\"type\":\"TotallyBogus\",\"x\":0,\"y\":0,\"pins\":[],\"props\":[]}],"
			"\"links\":[]}";
		serialization::ParseError err;
		auto parsed = serialization::FromJson(json, err);
		REQUIRE(!parsed.has_value());
	}

	void Test_SchemaTableCoversAllNodeTypes()
	{
		// Chaque type listé dans le schéma a un nom stable et est retrouvable.
		for (const auto& s : AllSchemas())
		{
			REQUIRE(FindSchema(s.type) != nullptr);
			RoutineNodeType rt;
			REQUIRE(FromString(ToString(s.type), rt));
			REQUIRE(rt == s.type);
			REQUIRE(s.validKindsMask != 0);
		}
	}

	void Test_EmptyGraphRoundTrip()
	{
		RoutineGraph g;
		g.kind = RoutineGraphKind::ZoneEvent;
		g.name = "";
		std::string json = serialization::ToJson(g);
		serialization::ParseError err;
		auto parsed = serialization::FromJson(json, err);
		REQUIRE(parsed.has_value());
		if (parsed) REQUIRE(GraphEquals(g, *parsed));
	}
}

int main()
{
	Test_RoundTrip_Identical();
	Test_Serialization_StableByteForByte();
	Test_FloatRoundTrip();
	Test_RejectFutureVersion();
	Test_RejectUnknownNodeType();
	Test_SchemaTableCoversAllNodeTypes();
	Test_EmptyGraphRoundTrip();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutineSerializationTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutineSerializationTests: %d échec(s)\n", g_failed);
	return g_failed;
}
