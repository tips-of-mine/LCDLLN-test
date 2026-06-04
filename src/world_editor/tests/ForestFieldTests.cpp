// M100.19 — Tests Forest & Field (générateurs purs). Headless. Lié à engine_core.

#include "src/client/world/foliage/FoliageLibrary.h"
#include "src/world_editor/ForestFieldGen.h"
#include "src/world_editor/ForestRecipe.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace engine::editor::world;
using engine::math::Vec3;
using engine::world::foliage::FoliageLibrary;
using engine::world::foliage::FoliageAsset;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

	FoliageLibrary MakeLib()
	{
		FoliageLibrary lib;
		FoliageAsset a; a.id = "tree_oak_01"; a.category = "tree_oak";
		a.rules.slopeMaxDeg = 25.0f; a.rules.altMin = -1000.0f; a.rules.altMax = 5000.0f;
		lib.assets.push_back(a);
		return lib;
	}

	std::vector<Vec3> Square10()
	{
		return { Vec3(0,0,0), Vec3(10,0,0), Vec3(10,0,10), Vec3(0,0,10) };
	}

	TerrainSampler FlatSampler(float slope)
	{
		return [slope](float, float) -> TerrainSample
		{
			TerrainSample s; s.terrainY = 5.0f; s.slopeDeg = slope; s.altMeters = 100.0f; s.splatLayer = 1;
			return s;
		};
	}

	ForestRecipe OneOakDensity(float density, uint64_t seed)
	{
		ForestRecipe r; r.seed = seed;
		r.entries.push_back(ForestRecipeEntry{ "tree_oak_01", 1.0f, density });
		return r;
	}

	void Test_Forest_DensityMatchesRecipe()
	{
		auto lib = MakeLib();
		auto poly = Square10();                  // aire 100
		auto recipe = OneOakDensity(0.1f, 42);   // densité 0.1 → cible ~10
		auto inst = GenerateForest(poly, recipe, lib, FlatSampler(0.0f));
		REQUIRE(inst.size() >= 9 && inst.size() <= 11); // ±5 % autour de 10
	}

	void Test_Forest_RulesFilterSlope()
	{
		auto lib = MakeLib();
		auto poly = Square10();
		auto recipe = OneOakDensity(0.2f, 7);
		auto inst = GenerateForest(poly, recipe, lib, FlatSampler(80.0f)); // pente > 25
		REQUIRE(inst.empty());
	}

	void Test_Forest_DeterministicWithSeed()
	{
		auto lib = MakeLib();
		auto poly = Square10();
		auto a = GenerateForest(poly, OneOakDensity(0.15f, 99), lib, FlatSampler(0.0f));
		auto b = GenerateForest(poly, OneOakDensity(0.15f, 99), lib, FlatSampler(0.0f));
		REQUIRE(a.size() == b.size());
		if (!a.empty() && a.size() == b.size())
			REQUIRE(Near(a[0].position.x, b[0].position.x) && Near(a[0].position.z, b[0].position.z));
	}

	void Test_Field_RegularSpacing()
	{
		FieldParams p; p.corner = Vec3(0,0,0); p.width = 6.0f; p.depth = 6.0f; p.spacing = 2.0f; p.rotationDeg = 0.0f;
		auto inst = GenerateField(p, 123u, FlatSampler(0.0f));
		REQUIRE(inst.size() == 16); // 4x4 (i,j = 0..3)
		// Espacement régulier sur la première ligne (j=0) : indices 0..3.
		if (inst.size() >= 4)
		{
			const float dx = inst[1].position.x - inst[0].position.x;
			const float dz = inst[1].position.z - inst[0].position.z;
			REQUIRE(Near(std::sqrt(dx * dx + dz * dz), 2.0f));
		}
	}

	void Test_Field_Rotation()
	{
		FieldParams p; p.corner = Vec3(0,0,0); p.width = 4.0f; p.depth = 4.0f; p.spacing = 2.0f; p.rotationDeg = 90.0f;
		auto inst = GenerateField(p, 1u, FlatSampler(0.0f));
		// i=1,j=0 (local (2,0)) tourné de 90° → world ~ (0, *, 2).
		REQUIRE(inst.size() >= 2);
		if (inst.size() >= 2)
		{
			REQUIRE(Near(inst[1].position.x, 0.0f, 1e-2f));
			REQUIRE(Near(inst[1].position.z, 2.0f, 1e-2f));
		}
	}
}

int main()
{
	Test_Forest_DensityMatchesRecipe();
	Test_Forest_RulesFilterSlope();
	Test_Forest_DeterministicWithSeed();
	Test_Field_RegularSpacing();
	Test_Field_Rotation();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] ForestFieldTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] ForestFieldTests: %d échec(s)\n", g_failed);
	return g_failed;
}
