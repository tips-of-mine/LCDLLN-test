// M100.20 — Tests vent : sway proportionnel hauteur, override zone, round-trip.
// Headless. Lié à engine_core.

#include "src/client/world/wind/WindParams.h"
#include "src/client/world/wind/WindSystem.h"
#include "src/client/world/wind/WindZones.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace engine::world::wind;
using engine::math::Vec3;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	void Test_SwayProportionalToHeight()
	{
		WindParamsCpu w; w.forceMps = 4.0f; w.timeSeconds = 0.0f;
		const float s0 = ComputeSwayMagnitude(w, 3.0f, 7.0f, 0.0f);
		const float sMid = ComputeSwayMagnitude(w, 3.0f, 7.0f, 0.5f);
		const float s1 = ComputeSwayMagnitude(w, 3.0f, 7.0f, 1.0f);
		REQUIRE(s0 == 0.0f);          // racines fixes
		REQUIRE(s1 > 0.0f);           // sommet bouge
		REQUIRE(sMid <= s1 + 1e-6f);  // monotone (smoothstep)
		REQUIRE(s0 <= sMid + 1e-6f);
	}

	void Test_LocalZone_OverridesGlobal()
	{
		WindParamsCpu global; global.forceMps = 4.0f; global.directionX = 1.0f; global.directionZ = 0.0f;
		WindZone z;
		z.polygon = { Vec3(0,0,0), Vec3(10,0,0), Vec3(10,0,10), Vec3(0,0,10) };
		z.forceMps = 12.0f; z.directionX = 0.0f; z.directionZ = 1.0f;
		std::vector<WindZone> zones = { z };

		WindParamsCpu inside = EvaluateWind(global, zones, 5.0f, 5.0f);
		REQUIRE(std::fabs(inside.forceMps - 12.0f) < 1e-4f);
		REQUIRE(std::fabs(inside.directionZ - 1.0f) < 1e-4f);

		WindParamsCpu outside = EvaluateWind(global, zones, 50.0f, 50.0f);
		REQUIRE(std::fabs(outside.forceMps - 4.0f) < 1e-4f);
	}

	void Test_Roundtrip_WindZonesBin()
	{
		std::vector<WindZone> zones;
		WindZone z;
		z.polygon = { Vec3(1,0,2), Vec3(3,0,2), Vec3(3,0,5) };
		z.directionX = 0.7f; z.directionZ = -0.3f; z.forceMps = 6.5f;
		z.turbulenceFreq = 0.4f; z.turbulenceAmp = 1.1f;
		zones.push_back(z);

		std::vector<uint8_t> bytes = SaveWindZonesBin(zones);
		std::vector<WindZone> out; std::string err;
		REQUIRE(LoadWindZonesBin(bytes, out, err));
		REQUIRE(out.size() == 1);
		if (out.size() == 1)
		{
			REQUIRE(out[0].polygon.size() == 3);
			REQUIRE(std::fabs(out[0].forceMps - 6.5f) < 1e-4f);
			REQUIRE(std::fabs(out[0].turbulenceAmp - 1.1f) < 1e-4f);
			REQUIRE(std::fabs(out[0].polygon[2].z - 5.0f) < 1e-4f);
		}
	}
}

int main()
{
	Test_SwayProportionalToHeight();
	Test_LocalZone_OverridesGlobal();
	Test_Roundtrip_WindZonesBin();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] WindTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] WindTests: %d échec(s)\n", g_failed);
	return g_failed;
}
