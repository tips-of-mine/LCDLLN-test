// M100.31 — Tests hameau : kit JSON, déterminisme, min_spacing, snap route,
// une entrée d'historique. Headless. Lié à engine_core.

#include "src/client/world/instances/PropInstances.h"
#include "src/client/world/structures/HamletKitLibrary.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/HamletGen.h"
#include "src/world_editor/PlacementCommand.h"
#include "src/world_editor/PlacementDocument.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace engine::editor::world;
using engine::math::Vec3;
using engine::world::instances::PropInstance;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	float Dist2D(const Vec3& a, const Vec3& b) { const float dx=a.x-b.x, dz=a.z-b.z; return std::sqrt(dx*dx+dz*dz); }

	std::vector<Vec3> BigPolygon()
	{
		return { Vec3(0,0,0), Vec3(80,0,0), Vec3(80,0,80), Vec3(0,0,80) };
	}

	HamletTerrainSample FlatSampler(float, float) { HamletTerrainSample s; s.slopeDeg = 2.0f; s.terrainY = 10.0f; return s; }

	HamletRecipe Recipe(uint64_t seed, bool snap)
	{
		HamletRecipe r; r.seed = seed; r.houseCount = 10; r.minSpacing = 8.0f; r.snapToRoad = snap;
		r.houseMeshes = { { "human_house_01.glb", 0.5f }, { "human_house_02.glb", 0.5f } };
		return r;
	}

	void Test_HamletKit_LoadsHumanKit()
	{
		const char* json = R"JSON({
		  "race": "Humains", "biome": "plains",
		  "houses": [ { "mesh": "structures/human_house_01.glb", "weight": 0.5 },
		              { "mesh": "structures/human_house_02.glb", "weight": 0.3 },
		              { "mesh": "structures/human_house_03.glb", "weight": 0.2 } ],
		  "minSpacingDefault": 8.0, "footprintRadius": 5.0 })JSON";
		engine::world::structures::HamletKit kit; std::string err;
		REQUIRE(engine::world::structures::ParseHamletKitJson(json, kit, err));
		REQUIRE(kit.race == "Humains");
		REQUIRE(kit.houses.size() == 3);
		REQUIRE(std::fabs(kit.minSpacingDefault - 8.0f) < 1e-3f);
	}

	void Test_Hamlet_DeterministicWithSeed()
	{
		uint32_t id1 = 1, id2 = 1;
		auto a = GenerateHamlet(BigPolygon(), Recipe(42, false), FlatSampler, {}, id1);
		auto b = GenerateHamlet(BigPolygon(), Recipe(42, false), FlatSampler, {}, id2);
		REQUIRE(a.size() == b.size());
		if (!a.empty() && a.size() == b.size())
			REQUIRE(Dist2D(a[0].position, b[0].position) < 1e-3f);
	}

	void Test_Hamlet_RespectsMinSpacing()
	{
		uint32_t id = 1;
		auto h = GenerateHamlet(BigPolygon(), Recipe(7, false), FlatSampler, {}, id);
		REQUIRE(h.size() >= 2);
		for (size_t i = 0; i < h.size(); ++i)
			for (size_t j = i + 1; j < h.size(); ++j)
				REQUIRE(Dist2D(h[i].position, h[j].position) >= 8.0f - 1e-2f);
	}

	void Test_Hamlet_SnapsToRoadWhenAvailable()
	{
		// Route le long de z=0 ; polygone collé à la route (tout < 30 m).
		std::vector<Vec3> poly = { Vec3(0,0,-8), Vec3(80,0,-8), Vec3(80,0,8), Vec3(0,0,8) };
		std::vector<Vec3> road = { Vec3(0,0,0), Vec3(80,0,0) };
		uint32_t id = 1;
		auto h = GenerateHamlet(poly, Recipe(3, true), FlatSampler, road, id);
		REQUIRE(!h.empty());
		// Toutes snappées à ~offset (4 m) de la route z=0.
		for (const auto& inst : h)
			REQUIRE(std::fabs(std::fabs(inst.position.z) - 4.0f) < 0.5f);
	}

	void Test_Hamlet_OneHistoryEntry()
	{
		uint32_t id = 1;
		auto houses = GenerateHamlet(BigPolygon(), Recipe(42, false), FlatSampler, {}, id);
		PlacementDocument doc;
		CommandStack stack;
		stack.Push(std::make_unique<PlacePropsCommand>(doc, houses));
		REQUIRE(stack.UndoSize() == 1); // une seule entrée pour tout l'hameau
		REQUIRE(doc.All().size() == houses.size());
		stack.Undo();
		REQUIRE(doc.All().empty());
	}
}

int main()
{
	Test_HamletKit_LoadsHumanKit();
	Test_Hamlet_DeterministicWithSeed();
	Test_Hamlet_RespectsMinSpacing();
	Test_Hamlet_SnapsToRoadWhenAvailable();
	Test_Hamlet_OneHistoryEntry();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] HamletTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] HamletTests: %d échec(s)\n", g_failed);
	return g_failed;
}
