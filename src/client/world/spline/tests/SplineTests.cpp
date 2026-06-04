// M100.29 — Tests spline : Catmull-Rom continuité, ground-fit, splat sum=255, round-trip.

#include "src/client/world/spline/SplineInstances.h"
#include "src/client/world/spline/SplineSampler.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace engine::world::spline;
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

	bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
	float Dist(const Vec3& a, const Vec3& b)
	{
		const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	SplineNode N(float x, float y, float z) { SplineNode n; n.position = Vec3(x, y, z); return n; }

	void Test_Spline_CatmullRom_Continuity()
	{
		std::vector<SplineNode> nodes = { N(0,0,0), N(10,0,0), N(20,0,5), N(30,0,5) };
		const int spp = 10;
		auto pts = SampleCatmullRom(nodes, false, spp);
		REQUIRE(pts.size() == static_cast<size_t>(3 * spp + 1));
		// Passe par les nœuds (t=0 de chaque segment + dernier point).
		REQUIRE(Dist(pts[0], nodes[0].position) < 1e-3f);
		REQUIRE(Dist(pts[spp], nodes[1].position) < 1e-3f);
		REQUIRE(Dist(pts[2 * spp], nodes[2].position) < 1e-3f);
		REQUIRE(Dist(pts.back(), nodes[3].position) < 1e-3f);
		// Continuité : pas de saut entre échantillons consécutifs.
		for (size_t i = 1; i < pts.size(); ++i) REQUIRE(Dist(pts[i], pts[i - 1]) < 6.0f);
	}

	void Test_Spline_GroundAutoFit()
	{
		std::vector<Vec3> pts = { Vec3(0,0,0), Vec3(5,0,5), Vec3(10,0,10) };
		auto fit = GroundFit(pts, [](float, float) { return 7.0f; });
		REQUIRE(fit.size() == 3);
		for (const auto& p : fit) REQUIRE(Near(p.y, 7.0f));
	}

	void Test_PaintAlongSpline_PreservesSum255()
	{
		std::array<uint8_t, 8> w = { 100, 100, 55, 0, 0, 0, 0, 0 };
		ApplyRoadWeight(w, 3, 200);
		int sum = 0; for (uint8_t v : w) sum += v;
		REQUIRE(sum == 255);
		REQUIRE(w[3] >= 200); // la route domine

		// Cas autres-couches nulles → route = 255.
		std::array<uint8_t, 8> z = { 0, 0, 0, 0, 0, 0, 0, 0 };
		ApplyRoadWeight(z, 0, 128);
		int sum2 = 0; for (uint8_t v : z) sum2 += v;
		REQUIRE(sum2 == 255);
	}

	void Test_Roundtrip_SplinesBin()
	{
		Spline s; s.type = SplineType::Road; s.curve = SplineCurve::CatmullRom; s.closed = false;
		s.nodes = { N(1,2,3), N(4,5,6) }; s.nodes[0].widthMeters = 8.0f;
		s.splatLayerIndex = 2; s.splatStrength = 0.9f; s.splatFeatherMeters = 0.7f;
		auto bytes = SaveSplinesBin({ s });
		std::vector<Spline> out; std::string err;
		REQUIRE(LoadSplinesBin(bytes, out, err));
		REQUIRE(out.size() == 1);
		if (out.size() == 1)
		{
			REQUIRE(out[0].type == SplineType::Road);
			REQUIRE(out[0].nodes.size() == 2);
			REQUIRE(Near(out[0].nodes[0].widthMeters, 8.0f));
			REQUIRE(out[0].splatLayerIndex == 2);
			REQUIRE(Near(out[0].splatStrength, 0.9f));
		}
	}
}

int main()
{
	Test_Spline_CatmullRom_Continuity();
	Test_Spline_GroundAutoFit();
	Test_PaintAlongSpline_PreservesSum255();
	Test_Roundtrip_SplinesBin();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] SplineTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] SplineTests: %d échec(s)\n", g_failed);
	return g_failed;
}
