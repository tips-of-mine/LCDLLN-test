// M100.27 — Tests thermal : shade round-trip/builder + ComputeTemperature + query.

#include "src/client/world/thermal/ShadeMap.h"
#include "src/client/world/thermal/ShadeMapBuilder.h"
#include "src/client/world/thermal/Thermal.h"
#include "src/client/world/thermal/ThermalQueryService.h"

#include <cmath>
#include <cstdio>

using namespace engine::world::thermal;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) <= eps; }

	void Test_ShadeMap_Roundtrip()
	{
		ShadeMap m; m.resolution = kShadeMapResolution;
		m.coverage.assign(static_cast<size_t>(m.resolution) * m.resolution, 0u);
		m.coverage[0] = 255; m.coverage[100] = 128; m.coverage.back() = 42;
		auto bytes = SaveShadeMapBin(m);
		ShadeMap out; std::string err;
		REQUIRE(LoadShadeMapBin(bytes, out, err));
		REQUIRE(out.resolution == m.resolution);
		REQUIRE(out.coverage.size() == m.coverage.size());
		if (out.coverage.size() == m.coverage.size())
		{
			REQUIRE(out.At(0, 0) == 255);
			REQUIRE(out.coverage[100] == 128);
			REQUIRE(out.coverage.back() == 42);
		}
	}

	void Test_ShadeMapBuilder_DenseCanopy_HighShade()
	{
		auto dense = BuildShadeMap(0.0f, 0.0f, 256.0f, [](float, float) { return true; }, 1);
		auto empty = BuildShadeMap(0.0f, 0.0f, 256.0f, [](float, float) { return false; }, 1);
		REQUIRE(dense.At(10, 10) == 255); // 16 hits -> 255
		REQUIRE(empty.At(10, 10) == 0);
	}

	void Test_ComputeTemperature_SummerNoonOpen0m()
	{
		ThermalContext ctx; ctx.season = ThermalSeason::Summer; ctx.weather = ThermalWeather::Clear; ctx.todHours = 14.0f;
		const float t = ComputeTemperature(ctx, 0.0f, 0.0f, 1.0e9f);
		REQUIRE(Near(t, 30.0f)); // 22 base + 8 diurnal
	}

	void Test_ComputeTemperature_WinterNightDense500m()
	{
		ThermalContext ctx; ctx.season = ThermalSeason::Winter; ctx.weather = ThermalWeather::Clear; ctx.todHours = 2.0f;
		const float t = ComputeTemperature(ctx, 500.0f, 1.0f, 1.0e9f);
		// 2 - 8 - 3.25 + 3 = -6.25
		REQUIRE(Near(t, -6.25f));
		// Qualitatif : en hiver la canopée réchauffe (vs ciel ouvert).
		const float open = ComputeTemperature(ctx, 500.0f, 0.0f, 1.0e9f);
		REQUIRE(t > open);
	}

	void Test_Qualitative_SummerCanopyCools_AltitudeCools_RainCools()
	{
		ThermalContext summer; summer.season = ThermalSeason::Summer; summer.todHours = 14.0f;
		REQUIRE(ComputeTemperature(summer, 0.0f, 1.0f, 1e9f) < ComputeTemperature(summer, 0.0f, 0.0f, 1e9f)); // canopée rafraîchit l'été
		REQUIRE(ComputeTemperature(summer, 1000.0f, 0.0f, 1e9f) < ComputeTemperature(summer, 0.0f, 0.0f, 1e9f)); // altitude
		ThermalContext rain = summer; rain.weather = ThermalWeather::Rain;
		REQUIRE(ComputeTemperature(rain, 0.0f, 0.0f, 1e9f) < ComputeTemperature(summer, 0.0f, 0.0f, 1e9f)); // pluie
	}

	void Test_ThermalQuery_OnSeasonChange_RebuildsMap()
	{
		ThermalQueryService svc;
		ThermalContext ctx; ctx.season = ThermalSeason::Summer; ctx.todHours = 14.0f;
		svc.Init(ctx, [](float, float) { return 0.0f; }, [](float, float) { return 0.0f; }, [](float, float) { return 1e9f; });
		const float summerT = svc.Query(0.0f, 0.0f);
		const int before = svc.RebuildCount();
		svc.OnSeasonChanged(ThermalSeason::Winter);
		const float winterT = svc.Query(0.0f, 0.0f);
		REQUIRE(svc.RebuildCount() == before + 1);
		REQUIRE(winterT < summerT); // rebuild reflété
	}
}

int main()
{
	Test_ShadeMap_Roundtrip();
	Test_ShadeMapBuilder_DenseCanopy_HighShade();
	Test_ComputeTemperature_SummerNoonOpen0m();
	Test_ComputeTemperature_WinterNightDense500m();
	Test_Qualitative_SummerCanopyCools_AltitudeCools_RainCools();
	Test_ThermalQuery_OnSeasonChange_RebuildsMap();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] ThermalTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] ThermalTests: %d échec(s)\n", g_failed);
	return g_failed;
}
