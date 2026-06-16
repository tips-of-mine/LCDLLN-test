// Auberge éditable (T2/T4) — Tests structures : preset IO, instanciation,
// groupes, export world.scenery. Headless. Lié à engine_core.

#include "src/world_editor/structures/BuildingPreset.h"
#include "src/world_editor/structures/BuildingPresetIo.h"
#include "src/world_editor/structures/BuildingInstantiate.h"
#include "src/world_editor/PlacementDocument.h"
#include "src/world_editor/structures/MoveGroupCommand.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/structures/SceneryExport.h"

#include <cmath>
#include <memory>
#include <cstdio>

using namespace engine::editor::world::structures;

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

	void Test_ParsePreset()
	{
		const std::string json = R"({
			"id": "auberge_demo",
			"displayName": "Auberge",
			"spawnAnchor": { "x": 0.0, "y": 0.0, "z": 2.0 },
			"elements": [
				{ "mesh": "meshes/props/Floor_WoodDark.gltf", "x": 0, "y": 0, "z": 0, "yaw_deg": 0, "scale": 1, "collision_radius": 0, "solid": false },
				{ "mesh": "meshes/props/Wall_Plaster_Straight.gltf", "x": 2.5, "y": 0, "z": 0, "yaw_deg": 90, "scale": 1, "collision_radius": 0.5, "solid": true }
			]
		})";
		BuildingPreset p; std::string err;
		REQUIRE(ParseBuildingPresetJson(json, p, err));
		REQUIRE(p.id == "auberge_demo");
		REQUIRE(p.displayName == "Auberge");
		REQUIRE(Near(p.spawnAnchor.z, 2.0f));
		REQUIRE(p.elements.size() == 2);
		if (p.elements.size() == 2)
		{
			REQUIRE(p.elements[0].meshPath == "meshes/props/Floor_WoodDark.gltf");
			REQUIRE(p.elements[0].solid == false);
			REQUIRE(Near(p.elements[1].offset.x, 2.5f));
			REQUIRE(Near(p.elements[1].yawDeg, 90.0f));
			REQUIRE(p.elements[1].solid == true);
			REQUIRE(Near(p.elements[1].collisionRadius, 0.5f));
		}
	}

	void Test_Preset_Roundtrip()
	{
		BuildingPreset p; p.id = "t"; p.displayName = "T";
		p.spawnAnchor = { 1.0f, 0.0f, -2.0f };
		BuildingPresetElement e; e.meshPath = "meshes/props/Door_2_Round.gltf";
		e.offset = { 3.0f, 0.0f, 4.0f }; e.yawDeg = 45.0f; e.scale = 1.2f;
		e.collisionRadius = 0.3f; e.solid = true;
		p.elements.push_back(e);
		const std::string json = SerializeBuildingPresetJson(p);
		BuildingPreset q; std::string err;
		REQUIRE(ParseBuildingPresetJson(json, q, err));
		REQUIRE(q.id == "t" && q.elements.size() == 1);
		if (q.elements.size() == 1)
		{
			REQUIRE(q.elements[0].meshPath == "meshes/props/Door_2_Round.gltf");
			REQUIRE(Near(q.elements[0].offset.z, 4.0f));
			REQUIRE(Near(q.elements[0].yawDeg, 45.0f));
			REQUIRE(Near(q.spawnAnchor.z, -2.0f));
		}
	}

	void Test_RotateYaw()
	{
		using engine::math::Vec3;
		// 90° : (1,0,0) -> (0,0,-1) avec la convention x'=x c + z s, z'=-x s + z c.
		Vec3 r = RotateYaw(Vec3(1, 0, 0), 90.0f);
		REQUIRE(Near(r.x, 0.0f, 1e-3f) && Near(r.z, -1.0f, 1e-3f));
		// 0° : identité.
		Vec3 r0 = RotateYaw(Vec3(2, 5, -3), 0.0f);
		REQUIRE(Near(r0.x, 2.0f) && Near(r0.y, 5.0f) && Near(r0.z, -3.0f));
	}

	void Test_Instantiate()
	{
		using engine::math::Vec3;
		BuildingPreset p; p.id = "x"; p.spawnAnchor = { 0, 0, 2 };
		BuildingPresetElement a; a.meshPath = "meshes/props/Floor_WoodDark.gltf";
		a.offset = { 0, 0, 0 };
		BuildingPresetElement b; b.meshPath = "meshes/props/Wall_Plaster_Straight.gltf";
		b.offset = { 1, 0, 0 };
		p.elements = { a, b };

		uint32_t next = 1;
		auto alloc = [&next]() { return next++; };
		// Pivot (10,0,20), pas de rotation.
		auto insts = InstantiatePreset(p, Vec3(10, 0, 20), 0.0f, 42, alloc);
		REQUIRE(insts.size() == 2);
		if (insts.size() == 2)
		{
			REQUIRE(Near(insts[0].position.x, 10.0f) && Near(insts[0].position.z, 20.0f));
			REQUIRE(Near(insts[1].position.x, 11.0f) && Near(insts[1].position.z, 20.0f));
			REQUIRE(insts[0].groupId == 42 && insts[1].groupId == 42);
			REQUIRE(insts[0].instanceId != insts[1].instanceId);
			REQUIRE(insts[0].layerTag ==
				static_cast<uint32_t>(engine::world::instances::PlacementLayer::Structures));
			REQUIRE(insts[0].assetId != 0);
		}
		// Ancre spawn monde = pivot + (0,0,2).
		Vec3 anchor = SpawnAnchorWorld(p, Vec3(10, 0, 20), 0.0f);
		REQUIRE(Near(anchor.x, 10.0f) && Near(anchor.z, 22.0f));
	}

	void Test_Group_MoveAndRemove()
	{
		using engine::editor::world::PlacementDocument;
		using engine::editor::world::MoveGroupCommand;
		using engine::editor::world::CommandStack;
		using engine::math::Vec3;

		PlacementDocument doc;
		const uint32_t gid = doc.AllocGroupId();
		REQUIRE(gid != 0);

		BuildingPreset p; p.id = "x";
		BuildingPresetElement a; a.meshPath = "meshes/props/Floor_WoodDark.gltf";
		BuildingPresetElement b; b.meshPath = "meshes/props/Wall_Plaster_Straight.gltf";
		b.offset = { 1, 0, 0 };
		p.elements = { a, b };
		auto alloc = [&doc]() { return doc.AllocInstanceId(); };
		for (auto& inst : InstantiatePreset(p, Vec3(0, 0, 0), 0.0f, gid, alloc))
			doc.Add(inst);
		// Une instance isolée hors groupe ne doit pas bouger.
		engine::world::instances::PropInstance solo;
		solo.instanceId = doc.AllocInstanceId(); solo.position = { 100, 0, 100 };
		doc.Add(solo);
		REQUIRE(doc.All().size() == 3);

		// Déplacer le groupe de (+5,0,+5).
		CommandStack stack;
		stack.Push(std::make_unique<MoveGroupCommand>(doc, gid, Vec3(5, 0, 5)));
		for (const auto& inst : doc.All())
		{
			if (inst.groupId == gid)
				REQUIRE(inst.position.x >= 5.0f - 1e-3f); // décalé
			else
				REQUIRE(Near(inst.position.x, 100.0f)); // intact
		}
		stack.Undo();
		for (const auto& inst : doc.All())
			if (inst.groupId == gid)
				REQUIRE(inst.position.x <= 1.0f + 1e-3f); // revenu

		// RemoveByGroup retire uniquement les membres du groupe.
		doc.RemoveByGroup(gid);
		REQUIRE(doc.All().size() == 1);
		REQUIRE(doc.All()[0].groupId == 0);
	}

	void Test_BuildSceneryEntries()
	{
		BuildingPreset p; p.id = "x";
		BuildingPresetElement a; a.meshPath = "meshes/props/Floor_WoodDark.gltf";
		a.offset = { 0, 0, 0 }; a.solid = false; a.collisionRadius = 0.0f;
		BuildingPresetElement b; b.meshPath = "meshes/props/Wall_Plaster_Straight.gltf";
		b.offset = { 2, 0, 0 }; b.yawDeg = 90.0f; b.solid = true; b.collisionRadius = 0.5f;
		p.elements = { a, b };
		auto entries = BuildSceneryEntries(p, 88.0f, 100.0f, 0.0f);
		REQUIRE(entries.size() == 2);
		if (entries.size() == 2)
		{
			REQUIRE(Near(entries[0].x, 88.0f) && Near(entries[0].z, 100.0f));
			REQUIRE(entries[0].solid == false);
			REQUIRE(Near(entries[1].x, 90.0f) && Near(entries[1].z, 100.0f));
			REQUIRE(Near(entries[1].yawDeg, 90.0f) && entries[1].solid == true);
		}
	}

	void Test_SpliceSceneryBlock()
	{
		const std::string cfg =
			"  AVANT\n"
			"  \"_comment_auberge\": \"x\",\n"
			"  \"310\": { \"mesh\": \"vieux\" },\n"
			"  \"_comment_auberge_end\": \"y\",\n"
			"  APRES\n";
		const std::string block =
			"  \"_comment_auberge\": \"AUBERGE generee\",\n"
			"  \"310\": { \"mesh\": \"neuf\" },\n";
		std::string out, err;
		REQUIRE(SpliceSceneryBlock(cfg, block, out, err));
		REQUIRE(out.find("neuf") != std::string::npos);
		REQUIRE(out.find("vieux") == std::string::npos);
		REQUIRE(out.find("AVANT") != std::string::npos);
		REQUIRE(out.find("APRES") != std::string::npos);
		REQUIRE(out.find("_comment_auberge_end") != std::string::npos);

		std::string out2, err2;
		REQUIRE(!SpliceSceneryBlock("pas de sentinelle", block, out2, err2));
	}

	void Test_SpliceInnRespawn()
	{
		const std::string txt =
			"# commentaire\n"
			"0 graveyard 120.0 1.5 120.0\n"
			"0 inn 88.0 1.5 100.0\n";
		std::string out = SpliceInnRespawn(txt, 0, 90.0f, 105.0f);
		REQUIRE(out.find("0 inn 90") != std::string::npos);
		REQUIRE(out.find("0 inn 88.0") == std::string::npos);
		REQUIRE(out.find("graveyard 120") != std::string::npos);
		// Ajout si absent.
		std::string out2 = SpliceInnRespawn("0 graveyard 1 1 1\n", 0, 5.0f, 6.0f);
		REQUIRE(out2.find("0 inn 5") != std::string::npos);
	}
}

int main()
{
	Test_ParsePreset();
	Test_Preset_Roundtrip();
	Test_RotateYaw();
	Test_Instantiate();
	Test_Group_MoveAndRemove();
	Test_BuildSceneryEntries();
	Test_SpliceSceneryBlock();
	Test_SpliceInnRespawn();
	if (g_failed == 0) std::fprintf(stderr, "[OK] BuildingStructuresTests\n");
	else std::fprintf(stderr, "[FAIL] BuildingStructuresTests: %d\n", g_failed);
	return g_failed;
}
