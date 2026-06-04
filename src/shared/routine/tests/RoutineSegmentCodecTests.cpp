// M101.3 — Tests du codec routines.bin (round-trip en mémoire), headless.

#include "src/shared/routine/RoutineGraph.h"
#include "src/shared/routine/RoutineSegmentCodec.h"
#include "src/shared/routine/RoutineSerialization.h"

#include <cstdio>
#include <string>
#include <vector>

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

	RoutineGraph MakeGraph(RoutineGraphKind kind, const std::string& name, RoutineNodeType nodeType)
	{
		RoutineGraph g;
		g.kind = kind;
		g.name = name;
		RoutineNode n; n.id = 1; n.type = nodeType; n.canvasX = 10.0f; n.canvasY = 20.0f;
		g.nodes.push_back(n);
		return g;
	}

	void Test_Codec_RoundTrip()
	{
		std::vector<RoutineGraph> graphs = {
			MakeGraph(RoutineGraphKind::ZoneEvent, "porte", RoutineNodeType::EventOnInteract),
			MakeGraph(RoutineGraphKind::NpcRoutine, "garde", RoutineNodeType::NpcStateRoot),
		};

		std::vector<uint8_t> bytes = codec::EncodeRoutinesBin(graphs);
		std::string err;
		auto decoded = codec::DecodeRoutinesBin(bytes, err);
		REQUIRE(decoded.has_value());
		if (decoded)
		{
			REQUIRE(decoded->size() == graphs.size());
			for (size_t i = 0; i < graphs.size() && i < decoded->size(); ++i)
			{
				// Comparaison via JSON canonique (égalité JSON ⇒ graphes égaux).
				REQUIRE(serialization::ToJson(graphs[i]) == serialization::ToJson((*decoded)[i]));
			}
		}
	}

	void Test_Codec_EmptyList()
	{
		std::vector<RoutineGraph> graphs;
		std::vector<uint8_t> bytes = codec::EncodeRoutinesBin(graphs);
		std::string err;
		auto decoded = codec::DecodeRoutinesBin(bytes, err);
		REQUIRE(decoded.has_value());
		if (decoded) REQUIRE(decoded->empty());
	}

	void Test_Codec_RejectBadMagic()
	{
		std::vector<uint8_t> bytes = { 0xDE, 0xAD, 0xBE, 0xEF, 1, 0, 0, 0, 0, 0, 0, 0 };
		std::string err;
		auto decoded = codec::DecodeRoutinesBin(bytes, err);
		REQUIRE(!decoded.has_value());
		REQUIRE(!err.empty());
	}

	void Test_Codec_RejectTruncated()
	{
		std::vector<RoutineGraph> graphs = {
			MakeGraph(RoutineGraphKind::ZoneEvent, "x", RoutineNodeType::EventOnInteract),
		};
		std::vector<uint8_t> bytes = codec::EncodeRoutinesBin(graphs);
		bytes.resize(bytes.size() - 4); // tronque le JSON
		std::string err;
		auto decoded = codec::DecodeRoutinesBin(bytes, err);
		REQUIRE(!decoded.has_value());
	}
}

int main()
{
	Test_Codec_RoundTrip();
	Test_Codec_EmptyList();
	Test_Codec_RejectBadMagic();
	Test_Codec_RejectTruncated();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutineSegmentCodecTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutineSegmentCodecTests: %d échec(s)\n", g_failed);
	return g_failed;
}
