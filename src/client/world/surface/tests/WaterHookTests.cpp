// src/client/world/surface/tests/WaterHookTests.cpp
#include "src/client/world/surface/SurfaceQueryService.h"
#include "src/client/world/surface/SurfaceTable.h"
#include "src/client/world/surface/SurfaceType.h"
#include "src/client/world/water/WaterSampler.h"
#include "src/client/world/water/WaterSurfaces.h"
#include "src/client/world/StreamCache.h"
#include "src/client/world/terrain/LayerPalette.h"
#include "src/shared/core/Config.h"

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
	using engine::world::surface::SurfaceQueryService;
	using engine::world::surface::SurfaceTable;
	using engine::world::surface::SurfaceType;

	// Lac 10×10 à Y=10. Pieds à Y=variable selon test.
	WaterScene MakeSquareLake(float surfaceY)
	{
		WaterScene s;
		LakeInstance lake;
		lake.polygon = { {0, surfaceY, 0}, {10, surfaceY, 0}, {10, surfaceY, 10}, {0, surfaceY, 10} };
		lake.waterLevelY = surfaceY;
		s.lakes.push_back(std::move(lake));
		return s;
	}

	struct Harness
	{
		WaterScene scene;
		WaterSampler sampler;
		SurfaceTable table;
		engine::world::StreamCache cache;
		engine::core::Config cfg;
		engine::world::terrain::LayerPalette palette;
		SurfaceQueryService service;

		void Setup(float surfaceY)
		{
			scene = MakeSquareLake(surfaceY);
			sampler.Init(scene);
			service.Init(table, cache, cfg, palette);
			service.SetWaterSampler(&sampler);
		}
	};

	void Test_DepthBelow1m_IsShallowWater()
	{
		Harness h;
		h.Setup(10.0f);

		// Pieds à Y=9.7 → depth=0.3 → ShallowWater.
		auto r = h.service.Query(Vec3{ 5.0f, 9.7f, 5.0f });
		REQUIRE(r.base == SurfaceType::ShallowWater);
	}

	void Test_DepthAtOrAbove1m_IsDeepWater()
	{
		Harness h;
		h.Setup(10.0f);

		// Pieds à Y=9.0 → depth=1.0 (frontière inclusive) → DeepWater.
		auto r1 = h.service.Query(Vec3{ 5.0f, 9.0f, 5.0f });
		REQUIRE(r1.base == SurfaceType::DeepWater);

		// Pieds à Y=8.8 → depth=1.2 → DeepWater.
		auto r2 = h.service.Query(Vec3{ 5.0f, 8.8f, 5.0f });
		REQUIRE(r2.base == SurfaceType::DeepWater);
	}

	void Test_NoSampler_FallsBackToSplat()
	{
		// Service sans SetWaterSampler : pas d'override. La sortie ne dépend
		// que de la splat (fallback Dirt avec setup minimal). On vérifie
		// l'absence de Shallow/Deep, ce qui prouve le non-branchement.
		Harness h;
		h.Setup(10.0f);
		h.service.SetWaterSampler(nullptr);

		// Sans sampler, StreamCache vide → LoadSplatMap nullptr → return fallback
		// (SurfaceType::Dirt, défini ligne 51 de SurfaceQueryService.cpp).
		// ApplyWaterOverride sans sampler retourne fallback inchangé.
		auto r = h.service.Query(Vec3{ 5.0f, 9.0f, 5.0f });
		REQUIRE(r.base == SurfaceType::Dirt);
	}
}

int main()
{
	Test_DepthBelow1m_IsShallowWater();
	Test_DepthAtOrAbove1m_IsDeepWater();
	Test_NoSampler_FallsBackToSplat();
	if (g_failed == 0) std::printf("[OK] 3 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
