// src/client/world/water/tests/SeaGridTests.cpp
// EAU.6 — grille de mer autour du joueur (SeaGrid).
#include "src/client/world/water/SeaGrid.h"
#include "src/client/world/water/SeaSwell.h"

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

	namespace w = engine::world::water;
	using engine::math::Vec3;

	w::LakeInstance Ocean(float x0, float z0, float x1, float z1, float level = 40.0f)
	{
		w::LakeInstance l;
		l.name = "ocean";
		l.isOcean = true;
		l.waterLevelY = level;
		l.polygon = { Vec3{ x0, level, z0 }, Vec3{ x1, level, z0 }, Vec3{ x1, level, z1 }, Vec3{ x0, level, z1 } };
		return l;
	}

	float Area(const w::SeaRect& r) { return (r.x1 - r.x0) * (r.z1 - r.z0); }

	void Test_OceanRect_AcceptsAxisAlignedOceanOnly()
	{
		w::SeaRect r;
		REQUIRE(w::OceanRect(Ocean(-10, -20, 30, 40), r));
		REQUIRE(r.x0 == -10.0f && r.z0 == -20.0f && r.x1 == 30.0f && r.z1 == 40.0f);
		w::LakeInstance lake = Ocean(0, 0, 10, 10);
		lake.isOcean = false;
		REQUIRE(!w::OceanRect(lake, r));                      // lac : jamais
		w::LakeInstance tilted = Ocean(0, 0, 10, 10);
		tilted.polygon[2].x = 7.0f;                           // plus un rectangle
		REQUIRE(!w::OceanRect(tilted, r));
	}

	void Test_SubtractSeaRect_KeepsAreaAndNoOverlap()
	{
		const w::SeaRect rect{ 0, 0, 100, 100 };
		std::vector<w::SeaRect> out;
		w::SubtractSeaRect(rect, w::SeaRect{ 40, 40, 60, 60 }, out);
		REQUIRE(out.size() == 4u);
		float sum = 0.0f;
		for (const auto& p : out) sum += Area(p);
		REQUIRE(std::fabs(sum - (10000.0f - 400.0f)) < 1e-3f);
		for (const auto& p : out)
			REQUIRE(!(p.Contains(50.0f, 50.0f)));
		out.clear();
		w::SubtractSeaRect(rect, w::SeaRect{ 200, 200, 300, 300 }, out);  // sans recoupement
		REQUIRE(out.size() == 1u && Area(out[0]) == 10000.0f);
		out.clear();
		w::SubtractSeaRect(rect, w::SeaRect{ -50, -50, 150, 150 }, out); // trou qui couvre tout
		REQUIRE(out.empty());
	}

	void Test_Snap()
	{
		REQUIRE(w::SnapToStep(47.0f, 32.0f) == 32.0f);
		REQUIRE(w::SnapToStep(49.0f, 32.0f) == 64.0f);
		REQUIRE(w::SnapToStep(-17.0f, 32.0f) == -32.0f);
	}

	void Test_BuildSeaGrid_OpenSea()
	{
		const std::vector<w::LakeInstance> lakes{ Ocean(-1000, -1000, 1000, 1000) };
		w::SeaGridSettings s;
		s.halfSizeM = 64.0f;
		s.stepM = 4.0f;
		s.edgeFadeM = 16.0f;
		w::SeaGridCpu g;
		const auto seabed = [](float, float, bool& known) { known = true; return 30.0f; };  // 10 m de fond
		REQUIRE(w::BuildSeaGrid(lakes, 0.0f, 0.0f, s, seabed, g));
		REQUIRE(g.vertices.size() == 33u * 33u);
		REQUIRE(g.indices.size() == 32u * 32u * 6u);
		REQUIRE(g.unknownSamples == 0u);
		REQUIRE(g.oceanLakeIndex == 0u);
		bool sawCenter = false;
		for (const auto& v : g.vertices)
		{
			REQUIRE(v.position.y == 40.0f);
			REQUIRE(std::fabs(v.depthM - 10.0f) < 1e-4f);
			const float r = std::fmax(std::fabs(v.position.x), std::fabs(v.position.z));
			if (r >= 64.0f - 1e-3f) REQUIRE(v.edgeFade == 0.0f);   // bord : mer plate
			if (r <= 48.0f) REQUIRE(v.edgeFade == 1.0f);           // intérieur : vagues entières
			if (r == 0.0f) sawCenter = true;
		}
		REQUIRE(sawCenter);
		// Tous les triangles ont une normale +Y (sens de BuildLakeMesh).
		for (size_t t = 0; t + 2 < g.indices.size(); t += 3)
		{
			const Vec3& a = g.vertices[g.indices[t]].position;
			const Vec3& b = g.vertices[g.indices[t + 1]].position;
			const Vec3& c = g.vertices[g.indices[t + 2]].position;
			const float cross = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
			REQUIRE(cross > 0.0f);
		}
	}

	void Test_BuildSeaGrid_ClippedToSea_DrySkipped_UnknownIsDeep()
	{
		// Mer à l'ouest (x < 0) seulement.
		const std::vector<w::LakeInstance> lakes{ Ocean(-1000, -1000, 0, 1000) };
		w::SeaGridSettings s;
		s.halfSizeM = 32.0f;
		s.stepM = 4.0f;
		w::SeaGridCpu g;
		const auto flat = [](float, float, bool& known) { known = true; return 39.0f; };
		REQUIRE(w::BuildSeaGrid(lakes, 0.0f, 0.0f, s, flat, g));
		for (const auto& v : g.vertices)
			REQUIRE(v.position.x <= 0.0f + 1e-3f);                  // rien côté terre
		REQUIRE(g.indices.size() == 8u * 16u * 6u);

		// Plage en pente : les carreaux à plus d'1 m au-dessus de la mer sont sautés.
		const auto beach = [](float x, float, bool& known) { known = true; return 40.0f + x * 0.1f; };
		const std::vector<w::LakeInstance> all{ Ocean(-1000, -1000, 1000, 1000) };
		REQUIRE(w::BuildSeaGrid(all, 0.0f, 0.0f, s, beach, g));
		for (const auto& v : g.vertices)
			REQUIRE(v.depthM >= -1.0f - 0.4f - 1e-3f);              // au plus un pas au-delà
		// Sol inconnu : traité comme le large et compté.
		const auto unknown = [](float, float, bool& known) { known = false; return 0.0f; };
		REQUIRE(w::BuildSeaGrid(all, 0.0f, 0.0f, s, unknown, g));
		REQUIRE(g.unknownSamples == static_cast<uint32_t>(g.vertices.size()));
		REQUIRE(g.vertices[0].depthM == w::kSeaDeepWaterM);
	}

	void Test_BuildSeaGrid_NoSeaNearby()
	{
		w::SeaGridSettings s;
		w::SeaGridCpu g;
		const auto flat = [](float, float, bool& known) { known = true; return 0.0f; };
		// Lac seul : jamais de grille.
		w::LakeInstance lake = Ocean(-1000, -1000, 1000, 1000);
		lake.isOcean = false;
		REQUIRE(!w::BuildSeaGrid({ lake }, 0.0f, 0.0f, s, flat, g));
		REQUIRE(!g.Valid());
		// Mer loin du centre : pas de grille non plus, centre mémorisé.
		REQUIRE(!w::BuildSeaGrid({ Ocean(5000, 5000, 6000, 6000) }, 32.0f, 64.0f, s, flat, g));
		REQUIRE(g.centerX == 32.0f && g.centerZ == 64.0f);
	}
}

int main()
{
	Test_OceanRect_AcceptsAxisAlignedOceanOnly();
	Test_SubtractSeaRect_KeepsAreaAndNoOverlap();
	Test_Snap();
	Test_BuildSeaGrid_OpenSea();
	Test_BuildSeaGrid_ClippedToSea_DrySkipped_UnknownIsDeep();
	Test_BuildSeaGrid_NoSeaNearby();

	if (g_failed == 0)
	{
		std::printf("All SeaGrid tests passed.\n");
		return 0;
	}
	std::fprintf(stderr, "%d test(s) failed.\n", g_failed);
	return 1;
}
