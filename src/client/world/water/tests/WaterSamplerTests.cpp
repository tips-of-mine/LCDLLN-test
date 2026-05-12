// src/client/world/water/tests/WaterSamplerTests.cpp
#include "src/client/world/water/WaterSampler.h"
#include "src/client/world/water/WaterSurfaces.h"

#include <cmath>
#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::math::Vec3;
	using engine::world::water::LakeInstance;
	using engine::world::water::WaterSampler;
	using engine::world::water::WaterScene;
	using engine::world::water::RiverInstance;
	using engine::world::water::RiverNode;

	// Carré de 10×10 centré sur (5, *, 5), surface à Y=10.
	WaterScene MakeSquareLake()
	{
		WaterScene s;
		LakeInstance lake;
		lake.name = "square_lake";
		lake.polygon = { {0, 10, 0}, {10, 10, 0}, {10, 10, 10}, {0, 10, 10} };
		lake.waterLevelY = 10.0f;
		s.lakes.push_back(std::move(lake));
		return s;
	}

	void Test_Lake_PointInside_ReturnsSurfaceY()
	{
		WaterScene scene = MakeSquareLake();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Pieds à Y=8, donc 2 m sous la surface au centre du lac.
		auto hit = sampler.Sample(Vec3{ 5.0f, 8.0f, 5.0f });
		REQUIRE(hit.has_value());
		REQUIRE(std::fabs(hit->surfaceY - 10.0f) < 1e-5f);
		REQUIRE(std::fabs(hit->depthMeters - 2.0f) < 1e-5f);
	}

	void Test_Lake_PointOutside_ReturnsNullopt()
	{
		WaterScene scene = MakeSquareLake();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Hors polygone (x=15).
		auto hit = sampler.Sample(Vec3{ 15.0f, 8.0f, 5.0f });
		REQUIRE(!hit.has_value());
	}

	// Rivière en ligne droite de (0,5,0) à (20,5,0), largeur 4 m partout.
	WaterScene MakeStraightRiver()
	{
		WaterScene s;
		RiverInstance r;
		r.name = "straight_river";
		r.nodes = {
			RiverNode{ Vec3{  0, 5, 0 }, 4.0f, 1.0f },
			RiverNode{ Vec3{ 20, 5, 0 }, 4.0f, 1.0f },
		};
		s.rivers.push_back(std::move(r));
		return s;
	}

	void Test_River_ProjectionOnSegment()
	{
		WaterScene scene = MakeStraightRiver();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Pieds à (10, 3, 1) : projection sur (10, 5, 0), distance latérale = 1 m
		// < width/2 = 2 m → hit avec surfaceY=5, depth=2.
		auto hit = sampler.Sample(Vec3{ 10.0f, 3.0f, 1.0f });
		REQUIRE(hit.has_value());
		REQUIRE(std::fabs(hit->surfaceY - 5.0f) < 1e-5f);
		REQUIRE(std::fabs(hit->depthMeters - 2.0f) < 1e-5f);
	}

	void Test_River_PointBeyondWidth_Misses()
	{
		WaterScene scene = MakeStraightRiver();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Distance latérale = 3 m > width/2 = 2 m → miss.
		auto hit = sampler.Sample(Vec3{ 10.0f, 3.0f, 3.0f });
		REQUIRE(!hit.has_value());
	}

	void Test_MultiOverlap_ReturnsDeepest()
	{
		// Lac à Y=10 (depth=5 si pieds à 5) ET rivière à Y=7 (depth=2 si pieds à 5)
		// au même point monde. Doit retourner le hit le plus profond (lac).
		WaterScene s;
		LakeInstance lake;
		lake.polygon = { {0, 10, 0}, {10, 10, 0}, {10, 10, 10}, {0, 10, 10} };
		lake.waterLevelY = 10.0f;
		s.lakes.push_back(lake);

		RiverInstance r;
		r.nodes = {
			RiverNode{ Vec3{  0, 7, 5 }, 6.0f, 1.0f },
			RiverNode{ Vec3{ 20, 7, 5 }, 6.0f, 1.0f },
		};
		s.rivers.push_back(r);

		WaterSampler sampler;
		REQUIRE(sampler.Init(s));

		// Pieds à (5, 5, 5). Lac : depth=5. Rivière : depth=2.
		auto hit = sampler.Sample(Vec3{ 5.0f, 5.0f, 5.0f });
		REQUIRE(hit.has_value());
		REQUIRE(std::fabs(hit->surfaceY - 10.0f) < 1e-5f);  // surface lac
		REQUIRE(std::fabs(hit->depthMeters - 5.0f) < 1e-5f); // profondeur lac
	}

	void Test_FeetAboveSurface_ReturnsNullopt()
	{
		WaterScene scene = MakeSquareLake();  // surface Y=10
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Pieds à Y=12, au-dessus de la surface (saut/vol au-dessus du lac).
		auto hit = sampler.Sample(Vec3{ 5.0f, 12.0f, 5.0f });
		REQUIRE(!hit.has_value());
	}
}

int main()
{
	Test_Lake_PointInside_ReturnsSurfaceY();
	Test_Lake_PointOutside_ReturnsNullopt();
	Test_River_ProjectionOnSegment();
	Test_River_PointBeyondWidth_Misses();
	Test_MultiOverlap_ReturnsDeepest();
	Test_FeetAboveSurface_ReturnsNullopt();
	if (g_failed == 0) std::printf("[OK] 6 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
