// M101.3 / M101.10 — Round-trip fichier éditeur→client de `routines.bin`.
// Écrit via WriteRoutines (zone_builder) puis relit via ReadRoutinesFile
// (client) et compare. Headless (filesystem temporaire, pas de Vulkan).

#include <zone_builder/ChunkPackageWriter.h>

#include "src/client/world/routine/RoutineSegmentReader.h"
#include "src/shared/routine/RoutineGraph.h"
#include "src/shared/routine/RoutineSerialization.h"

#include <cstdio>
#include <filesystem>
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

	RoutineGraph MakeSample()
	{
		RoutineGraph g;
		g.kind = RoutineGraphKind::ZoneEvent;
		g.name = "porte_taverne";
		RoutineNode ev; ev.id = 1; ev.type = RoutineNodeType::EventOnInteract;
		ev.canvasX = 64.0f; ev.canvasY = 32.0f;
		RoutineProperty pr; pr.key = "interactiveId"; pr.type = RoutineDataType::EntityRef;
		pr.sValue = "porte_42";
		ev.properties.push_back(pr);
		g.nodes.push_back(ev);
		return g;
	}

	void Test_FileRoundTrip()
	{
		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / "lcdlln_m101_routines_test";
		std::error_code ec;
		std::filesystem::remove_all(root, ec);

		std::vector<RoutineGraph> graphs = { MakeSample() };
		std::string err;
		bool wrote = tools::zone_builder::WriteRoutines(root.string(), 0, 0, graphs, err);
		REQUIRE(wrote);
		if (!wrote) { std::fprintf(stderr, "  write err: %s\n", err.c_str()); return; }

		const std::filesystem::path file = root / "chunks" / "chunk_0_0" / "routines.bin";
		REQUIRE(std::filesystem::exists(file));

		engine::world::routine::LoadedRoutines loaded;
		bool read = engine::world::routine::ReadRoutinesFile(file.string(), loaded, err);
		REQUIRE(read);
		if (read)
		{
			REQUIRE(loaded.graphs.size() == 1);
			if (!loaded.graphs.empty())
				REQUIRE(serialization::ToJson(loaded.graphs[0]) == serialization::ToJson(graphs[0]));
		}

		std::filesystem::remove_all(root, ec);
	}
}

int main()
{
	Test_FileRoundTrip();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutinesSegmentTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutinesSegmentTests: %d échec(s)\n", g_failed);
	return g_failed;
}
