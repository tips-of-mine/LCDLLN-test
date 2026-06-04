// M100.17 — Tests placement : géométrie pure + format props.bin + commande.
// Headless. Lié à engine_core.

#include "src/client/world/instances/PropInstances.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/PlacementCommand.h"
#include "src/world_editor/PlacementDocument.h"
#include "src/world_editor/PlacementGeometry.h"
#include "src/world_editor/PlacementTool.h"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace engine::editor::world;
using engine::math::Vec3;
using engine::world::instances::PropInstance;
namespace pg = engine::editor::world::placement;

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
	float Dist2D(const Vec3& a, const Vec3& b)
	{
		const float dx = a.x - b.x, dz = a.z - b.z;
		return std::sqrt(dx * dx + dz * dz);
	}

	void Test_Roundtrip_PropsBin()
	{
		std::vector<PropInstance> props;
		PropInstance a; a.assetId = 111; a.position = { 1.0f, 2.0f, 3.0f };
		a.rotationQuat[0] = 0.1f; a.rotationQuat[3] = 0.99f; a.scale = { 1.1f, 1.1f, 1.1f };
		a.layerTag = 1; a.instanceId = 7;
		PropInstance b; b.assetId = 222; b.position = { -4.0f, 0.0f, 5.0f }; b.instanceId = 8;
		props = { a, b };

		std::vector<uint8_t> bytes = engine::world::instances::SavePropsBin(props);
		std::vector<PropInstance> out; std::string err;
		REQUIRE(engine::world::instances::LoadPropsBin(bytes, out, err));
		REQUIRE(out.size() == 2);
		if (out.size() == 2)
		{
			REQUIRE(out[0].assetId == 111 && out[0].instanceId == 7);
			REQUIRE(Near(out[0].position.x, 1.0f) && Near(out[0].position.z, 3.0f));
			REQUIRE(Near(out[0].rotationQuat[0], 0.1f) && Near(out[0].rotationQuat[3], 0.99f));
			REQUIRE(out[1].assetId == 222 && out[1].instanceId == 8);
		}
	}

	void Test_DragLine_GeneratesCorrectSpacing()
	{
		auto pts = pg::GenerateDragLine(Vec3(0, 0, 0), Vec3(10, 0, 0), 2.0f);
		REQUIRE(pts.size() == 6); // 0,2,4,6,8,10
		for (size_t i = 1; i < pts.size(); ++i)
			REQUIRE(Near(Dist2D(pts[i], pts[i - 1]), 2.0f, 1e-3f));
	}

	void Test_Scatter_RespectsCountAndRadius()
	{
		const Vec3 c(100, 0, 100);
		auto a = pg::GenerateScatter(c, 5.0f, 12, 42);
		REQUIRE(a.size() == 12);
		for (const auto& p : a) REQUIRE(Dist2D(p, c) <= 5.0f + 1e-3f);
		// Déterminisme.
		auto b = pg::GenerateScatter(c, 5.0f, 12, 42);
		REQUIRE(a.size() == b.size());
		for (size_t i = 0; i < a.size(); ++i)
			REQUIRE(Near(a[i].x, b[i].x) && Near(a[i].z, b[i].z));
	}

	void Test_RandomRotation_DeterministicWithSeed()
	{
		auto r1 = pg::RandomYawScale(7, 0.0f, 360.0f, 0.95f, 1.05f);
		auto r2 = pg::RandomYawScale(7, 0.0f, 360.0f, 0.95f, 1.05f);
		REQUIRE(Near(r1.yawDeg, r2.yawDeg) && Near(r1.scale, r2.scale));
		REQUIRE(r1.yawDeg >= 0.0f && r1.yawDeg <= 360.0f);
		REQUIRE(r1.scale >= 0.95f && r1.scale <= 1.05f);
	}

	void Test_GroundSnap_AlignsToTerrainNormal()
	{
		float q[4];
		// Normale verticale → l'axe up reste up.
		pg::BuildOrientation(0.0f, Vec3(0, 1, 0), true, q);
		Vec3 up = pg::RotateVectorByQuat(q, Vec3(0, 1, 0));
		REQUIRE(Near(up.x, 0.0f) && Near(up.y, 1.0f) && Near(up.z, 0.0f));

		// Normale horizontale (1,0,0) → up doit s'aligner sur (1,0,0).
		pg::BuildOrientation(0.0f, Vec3(1, 0, 0), true, q);
		Vec3 up2 = pg::RotateVectorByQuat(q, Vec3(0, 1, 0));
		REQUIRE(Near(up2.x, 1.0f, 1e-2f) && Near(up2.y, 0.0f, 1e-2f));

		// World-up : pas d'alignement, up reste up quelle que soit la normale.
		pg::BuildOrientation(45.0f, Vec3(1, 0, 0), false, q);
		Vec3 up3 = pg::RotateVectorByQuat(q, Vec3(0, 1, 0));
		REQUIRE(Near(up3.y, 1.0f, 1e-3f));
	}

	void Test_Single_Placement_PushesOneCommand()
	{
		PlacementTool tool;
		PlacementParams p; p.assetPath = "rock_large_03.glb"; p.mode = PlacementMode::Single; p.rngSeed = 5;
		tool.SetParams(p);
		PlacementDocument doc;
		auto insts = tool.BuildInstances(Vec3(3, 0, 4), Vec3(3, 0, 4), Vec3(0, 1, 0), doc);
		REQUIRE(insts.size() == 1);

		CommandStack stack;
		stack.Push(std::make_unique<PlacePropsCommand>(doc, insts));
		REQUIRE(doc.All().size() == 1);
		REQUIRE(stack.UndoSize() == 1);
		stack.Undo();
		REQUIRE(doc.All().empty());
	}

	void Test_DragLine_And_Scatter_Counts()
	{
		PlacementDocument doc;
		PlacementTool tool;
		PlacementParams p; p.assetPath = "post.glb"; p.mode = PlacementMode::DragLine; p.dragLineSpacing = 2.0f; p.rngSeed = 9;
		tool.SetParams(p);
		auto line = tool.BuildInstances(Vec3(0, 0, 0), Vec3(10, 0, 0), Vec3(0, 1, 0), doc);
		REQUIRE(line.size() == 6);

		PlacementParams s; s.assetPath = "bush.glb"; s.mode = PlacementMode::Scatter; s.scatterCount = 8; s.scatterRadius = 4.0f; s.rngSeed = 9;
		tool.SetParams(s);
		auto scat = tool.BuildInstances(Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(0, 1, 0), doc);
		REQUIRE(scat.size() == 8);
		// instanceId uniques et non nuls (alloués par le document).
		REQUIRE(scat[0].instanceId != 0);
		REQUIRE(scat[0].instanceId != scat[1].instanceId);
	}
}

int main()
{
	Test_Roundtrip_PropsBin();
	Test_DragLine_GeneratesCorrectSpacing();
	Test_Scatter_RespectsCountAndRadius();
	Test_RandomRotation_DeterministicWithSeed();
	Test_GroundSnap_AlignsToTerrainNormal();
	Test_Single_Placement_PushesOneCommand();
	Test_DragLine_And_Scatter_Counts();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] PlacementTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] PlacementTests: %d échec(s)\n", g_failed);
	return g_failed;
}
