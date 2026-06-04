// M100.30 — Tests structures : pont/mur génération, coins, surface pont,
// migration splines v1->v2. Headless.

#include "src/client/world/spline/SplineInstances.h"
#include "src/client/world/structures/Kits.h"
#include "src/client/world/structures/Structures.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace engine::world::structures;
using engine::math::Vec3;
namespace spl = engine::world::spline;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	void Test_Bridge_GeneratesCorrectSegmentCount()
	{
		REQUIRE(BridgeSegmentCount(40.0f, 4.0f) == 10);
		REQUIRE(BridgeSegmentCount(0.5f, 4.0f) == 1); // minimum 1
	}

	void Test_Wall_PostSpacingApplied()
	{
		auto posts = WallPostDistances(12.0f, 4.0f); // 0,4,8,12
		REQUIRE(posts.size() == 4);
		if (posts.size() == 4)
		{
			REQUIRE(std::fabs(posts[0] - 0.0f) < 1e-3f);
			REQUIRE(std::fabs(posts[1] - 4.0f) < 1e-3f);
			REQUIRE(std::fabs(posts[3] - 12.0f) < 1e-3f);
		}
	}

	void Test_Wall_DetectsCornersOver70deg()
	{
		// Virage à 90° au nœud 1.
		std::vector<Vec3> bent = { Vec3(0,0,0), Vec3(10,0,0), Vec3(10,0,10) };
		auto corners = DetectSharpCorners(bent, 70.0f);
		REQUIRE(corners.size() == 1);
		if (!corners.empty()) REQUIRE(corners[0] == 1);

		// Ligne droite : aucun coin.
		std::vector<Vec3> straight = { Vec3(0,0,0), Vec3(5,0,0), Vec3(10,0,0) };
		REQUIRE(DetectSharpCorners(straight, 70.0f).empty());
	}

	void Test_Bridge_SurfaceQueryReturnsBridge()
	{
		BridgeWalkable br; br.a = Vec3(0,5,0); br.b = Vec3(10,5,0); br.widthMeters = 6.0f; br.bridgeY = 5.0f;
		REQUIRE(QueryBridgeSurface(br, Vec3(5,5,1)) == true);    // sur le pont
		REQUIRE(QueryBridgeSurface(br, Vec3(5,5,10)) == false);  // hors largeur
		REQUIRE(QueryBridgeSurface(br, Vec3(5,8,0)) == false);   // mauvaise hauteur
	}

	void Test_KitHash_Stable()
	{
		REQUIRE(HashKitName("stone_bridge_kit") == HashKitName("stone_bridge_kit"));
		REQUIRE(HashKitName("a") != HashKitName("b"));
	}

	// Construit des octets splines.bin au FORMAT v1 (sans bloc kit).
	std::vector<uint8_t> MakeV1Bytes()
	{
		std::vector<uint8_t> b;
		spl::detail::PutU32(b, spl::kSplinesMagic);
		spl::detail::PutU32(b, 1u); // version 1
		spl::detail::PutU32(b, 1u); spl::detail::PutU32(b, 1u); spl::detail::PutU64(b, 0ull);
		spl::detail::PutU32(b, 1u); // 1 spline
		spl::detail::PutU32(b, static_cast<uint32_t>(spl::SplineType::Road));
		spl::detail::PutU32(b, static_cast<uint32_t>(spl::SplineCurve::CatmullRom));
		spl::detail::PutU32(b, 0u); // closed
		spl::detail::PutU32(b, 2u); // 2 nodes
		for (int i = 0; i < 2; ++i)
		{
			spl::detail::PutF32(b, static_cast<float>(i) * 10.0f); spl::detail::PutF32(b, 0.0f); spl::detail::PutF32(b, 0.0f);
			spl::detail::PutF32(b, 6.0f); spl::detail::PutF32(b, 0.0f);
		}
		spl::detail::PutU32(b, 2u);   // splatLayer
		spl::detail::PutF32(b, 1.0f); // strength
		spl::detail::PutF32(b, 0.5f); // feather
		// PAS de bloc kit (v1).
		return b;
	}

	void Test_SplinesBin_V1ToV2Migration()
	{
		auto v1 = MakeV1Bytes();
		std::vector<spl::Spline> out; std::string err;
		REQUIRE(spl::LoadSplinesBin(v1, out, err));
		REQUIRE(out.size() == 1);
		if (out.size() == 1)
		{
			REQUIRE(out[0].type == spl::SplineType::Road);
			REQUIRE(out[0].nodes.size() == 2);
			REQUIRE(out[0].kit.mode == spl::SplineKitMode::None); // défaut migration
		}
	}

	void Test_SplinesBin_V2_BridgeRoundtrip()
	{
		spl::Spline s; s.type = spl::SplineType::Road;
		s.nodes = { spl::SplineNode{}, spl::SplineNode{} };
		s.kit.mode = spl::SplineKitMode::Bridge; s.kit.kitNameHash = HashKitName("stone_bridge_kit");
		s.kit.segmentLengthMeters = 4.0f; s.kit.yMode = 0; s.kit.yConstant = 12.5f; s.kit.widthMeters = 6.0f;
		auto bytes = spl::SaveSplinesBin({ s });
		std::vector<spl::Spline> out; std::string err;
		REQUIRE(spl::LoadSplinesBin(bytes, out, err));
		REQUIRE(out.size() == 1);
		if (out.size() == 1)
		{
			REQUIRE(out[0].kit.mode == spl::SplineKitMode::Bridge);
			REQUIRE(std::fabs(out[0].kit.yConstant - 12.5f) < 1e-3f);
		}
	}
}

int main()
{
	Test_Bridge_GeneratesCorrectSegmentCount();
	Test_Wall_PostSpacingApplied();
	Test_Wall_DetectsCornersOver70deg();
	Test_Bridge_SurfaceQueryReturnsBridge();
	Test_KitHash_Stable();
	Test_SplinesBin_V1ToV2Migration();
	Test_SplinesBin_V2_BridgeRoundtrip();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] StructuresTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] StructuresTests: %d échec(s)\n", g_failed);
	return g_failed;
}
