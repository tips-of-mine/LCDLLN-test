// M100.18 — Tests foliage : library JSON + Poisson-disk + règles + round-trip.
// Headless. Lié à engine_core.

#include "src/client/world/foliage/FoliageInstances.h"
#include "src/client/world/foliage/FoliageLibrary.h"
#include "src/client/world/foliage/PoissonDiskSampler.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace engine::world::foliage;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	const char* kLibJson = R"JSON({
  "version": 1,
  "categories": [
    { "id": "grass", "label": "Herbes" },
    { "id": "tree_oak", "label": "Chene mature" },
    { "id": "tree_dark", "label": "Arbre noir torture" }
  ],
  "assets": [
    { "id": "grass_01", "category": "grass", "mesh": "veg/grass_01.glb", "billboard": false,
      "rules": { "slopeMaxDeg": 35.0, "altMin": -200.0, "altMax": 2500.0, "splatLayers": [1, 2] } },
    { "id": "tree_oak_01", "category": "tree_oak", "mesh": "veg/tree_oak_01.glb",
      "rules": { "slopeMaxDeg": 25.0, "altMin": 0.0, "altMax": 1800.0, "splatLayers": [1, 2] } }
  ]
})JSON";

	void Test_Library_LoadsCategoriesAndRules()
	{
		FoliageLibrary lib; std::string err;
		REQUIRE(ParseFoliageLibraryJson(kLibJson, lib, err));
		REQUIRE(lib.version == 1);
		REQUIRE(lib.categories.size() == 3);
		REQUIRE(lib.assets.size() == 2);
		const FoliageAsset* oak = lib.FindAsset("tree_oak_01");
		REQUIRE(oak != nullptr);
		if (oak)
		{
			REQUIRE(oak->category == "tree_oak");
			REQUIRE(std::fabs(oak->rules.slopeMaxDeg - 25.0f) < 1e-3f);
			REQUIRE(oak->rules.splatLayers.size() == 2);
		}
	}

	void Test_PoissonDisk_RespectsMinRadius()
	{
		const float minR = 1.0f;
		auto pts = SamplePoissonDisk(20.0f, 20.0f, minR, 1234);
		REQUIRE(pts.size() > 10); // couverture raisonnable
		const float r2 = minR * minR;
		bool ok = true;
		for (size_t i = 0; i < pts.size() && ok; ++i)
			for (size_t j = i + 1; j < pts.size(); ++j)
			{
				const float dx = pts[i].x - pts[j].x, dz = pts[i].z - pts[j].z;
				if (dx * dx + dz * dz < r2 - 1e-4f) { ok = false; break; }
			}
		REQUIRE(ok);
	}

	void Test_RuleFilter_RejectsSteepCells()
	{
		FoliageRules r; r.slopeMaxDeg = 25.0f; r.altMin = 0.0f; r.altMax = 1800.0f; r.splatLayers = { 1, 2 };
		REQUIRE(PassesRules(r, 10.0f, 500.0f, 1) == true);    // OK
		REQUIRE(PassesRules(r, 40.0f, 500.0f, 1) == false);   // pente trop forte
		REQUIRE(PassesRules(r, 10.0f, 2500.0f, 1) == false);  // altitude trop haute
		REQUIRE(PassesRules(r, 10.0f, 500.0f, 5) == false);   // couche splat non listee
		FoliageRules any; // splatLayers vide => toutes couches
		REQUIRE(PassesRules(any, 10.0f, 500.0f, 7) == true);
	}

	void Test_Roundtrip_FoliageBin()
	{
		std::vector<FoliageInstance> items;
		FoliageInstance a; a.assetIdHash = 0xABCD; a.position = { 1.0f, 0.0f, 2.0f }; a.rotationY = 1.5f; a.scale = 1.2f;
		FoliageInstance b; b.assetIdHash = 0x1234; b.position = { 3.0f, 0.5f, 4.0f }; b.rotationY = -0.3f; b.scale = 0.9f;
		items = { a, b };
		std::vector<uint8_t> bytes = SaveFoliageBin(items);
		std::vector<FoliageInstance> out; std::string err;
		REQUIRE(LoadFoliageBin(bytes, out, err));
		REQUIRE(out.size() == 2);
		if (out.size() == 2)
		{
			REQUIRE(out[0].assetIdHash == 0xABCD);
			REQUIRE(std::fabs(out[0].rotationY - 1.5f) < 1e-4f);
			REQUIRE(std::fabs(out[1].scale - 0.9f) < 1e-4f);
		}
	}
}

int main()
{
	Test_Library_LoadsCategoriesAndRules();
	Test_PoissonDisk_RespectsMinRadius();
	Test_RuleFilter_RejectsSteepCells();
	Test_Roundtrip_FoliageBin();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] FoliageTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] FoliageTests: %d échec(s)\n", g_failed);
	return g_failed;
}
