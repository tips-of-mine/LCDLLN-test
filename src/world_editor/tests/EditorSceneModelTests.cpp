/// Tests unitaires CPU pour EditorSceneModel (sous-projet 1, bloc B).
/// Vérifie l'agrégation Terrain + LayoutInstances + MeshInserts +
/// DungeonPortals en une liste plate, les libellés, le flag hasTransform, et
/// Find(EntityId). Pur CPU (aucune dépendance ImGui/Vulkan), ctest Linux.

#include "src/world_editor/scene/EditorSceneModel.h"
#include "src/world_editor/ui/WorldMapEditDocument.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using namespace engine::editor::scene;

	size_t CountKind(const std::vector<SceneEntity>& v, EntityKind k)
	{
		size_t n = 0;
		for (const SceneEntity& e : v) { if (e.id.kind == k) ++n; }
		return n;
	}

	void Test_AggregatesAllSources()
	{
		engine::editor::WorldMapEditDocument layout;
		{
			engine::editor::WorldMapEditLayoutInstance a;
			a.speciesId = "oak";
			a.worldX = 10.0; a.worldY = 2.0; a.worldZ = -5.0;
			a.yawDegrees = 90.0; a.uniformScale = 1.5;
			layout.layoutInstances.push_back(a);
			engine::editor::WorldMapEditLayoutInstance b;
			b.gltfContentRelativePath = "props/rock.gltf";
			layout.layoutInstances.push_back(b);
		}

		engine::editor::world::volumes::MeshInsertDocument meshDoc;
		{
			engine::editor::world::volumes::MeshInsertInstance mi;
			mi.displayName = "Cave A";
			mi.insertCategory = "cave";
			mi.worldPosition.x = 1.0f; mi.worldPosition.y = 0.0f; mi.worldPosition.z = 2.0f;
			mi.uniformScale = 2.0f;
			(void)meshDoc.Add(mi);
		}

		engine::editor::world::volumes::dungeons::DungeonPortalDocument dunDoc;
		{
			engine::editor::world::volumes::dungeons::DungeonPortalInstance dp;
			dp.displayName = "Crypt";
			dp.dungeonTemplateId = "crypt_01";
			dp.worldPosition.x = 3.0f;
			(void)dunDoc.Add(dp);
		}

		EditorSceneModel model;
		model.Bind(&layout, &meshDoc, &dunDoc);
		model.Rebuild();

		const std::vector<SceneEntity>& e = model.Entities();
		// 1 terrain + 2 layout + 1 mesh + 1 dungeon = 5
		REQUIRE(e.size() == 5u);
		REQUIRE(CountKind(e, EntityKind::Terrain) == 1u);
		REQUIRE(CountKind(e, EntityKind::LayoutInstance) == 2u);
		REQUIRE(CountKind(e, EntityKind::MeshInsert) == 1u);
		REQUIRE(CountKind(e, EntityKind::DungeonPortal) == 1u);

		// Terrain : en tête, pas de transform.
		REQUIRE(e[0].id.kind == EntityKind::Terrain);
		REQUIRE(!e[0].hasTransform);

		// Le mesh insert porte son displayName et un transform.
		const SceneEntity* mesh = model.Find(EntityId{EntityKind::MeshInsert, 0u});
		REQUIRE(mesh != nullptr);
		REQUIRE(mesh->hasTransform);
		REQUIRE(mesh->label.find("Cave A") != std::string::npos);
		REQUIRE(mesh->transform.uniformScale == 2.0f);

		// La 1re instance de layout : libellé species + yaw mappé sur eulerDeg.y.
		const SceneEntity* inst0 = model.Find(EntityId{EntityKind::LayoutInstance, 0u});
		REQUIRE(inst0 != nullptr);
		REQUIRE(inst0->label.find("oak") != std::string::npos);
		REQUIRE(inst0->transform.eulerDeg.y == 90.0f);

		// Find sur un index inexistant -> nullptr.
		REQUIRE(model.Find(EntityId{EntityKind::MeshInsert, 99u}) == nullptr);
	}

	void Test_NullDocsGiveTerrainOnly()
	{
		EditorSceneModel model;
		model.Bind(nullptr, nullptr, nullptr);
		model.Rebuild();
		REQUIRE(model.Entities().size() == 1u);
		REQUIRE(model.Entities()[0].id.kind == EntityKind::Terrain);
	}
}

int main()
{
	Test_AggregatesAllSources();
	Test_NullDocsGiveTerrainOnly();

	if (g_failed == 0)
	{
		std::printf("[PASS] EditorSceneModelTests\n");
		return 0;
	}
	std::printf("[FAIL] EditorSceneModelTests: %d failure(s)\n", g_failed);
	return 1;
}
