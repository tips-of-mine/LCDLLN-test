/// Tests unitaires CPU pour AssetCatalog (catalogue d'assets de l'éditeur).
///
/// Couvre : parsing JSON (format Config `assets.count` + indexé), FindById,
/// ByCategory, Categories (ordre + dédup), entrées invalides ignorées.

#include "src/world_editor/assets/AssetCatalog.h"

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

	using engine::editor::world::assets::AssetCatalog;

	const char* kSampleJson = R"({
		"assets": {
			"count": 4,
			"0": { "id": "Wall_Plaster_Straight", "category": "Wall",
			       "gltf": "meshes/props/Wall_Plaster_Straight.gltf",
			       "displayName": "Mur platre droit" },
			"1": { "id": "Door_1_Flat", "category": "Door",
			       "gltf": "meshes/props/Door_1_Flat.gltf" },
			"2": { "id": "Wall_Arch", "category": "Wall",
			       "gltf": "meshes/props/Wall_Arch.gltf" },
			"3": { "id": "Roof_Log", "category": "Roof",
			       "gltf": "meshes/props/Roof_Log.gltf" }
		}
	})";

	void Test_ParseJson_Basic()
	{
		AssetCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(kSampleJson, err));
		REQUIRE(err.empty());
		REQUIRE(cat.Size() == 4u);

		const auto* wall = cat.FindById("Wall_Plaster_Straight");
		REQUIRE(wall != nullptr);
		REQUIRE(wall->category == "Wall");
		REQUIRE(wall->gltfRelativePath == "meshes/props/Wall_Plaster_Straight.gltf");
		REQUIRE(wall->displayName == "Mur platre droit");

		// displayName par défaut = id quand absent.
		const auto* door = cat.FindById("Door_1_Flat");
		REQUIRE(door != nullptr);
		REQUIRE(door->displayName == "Door_1_Flat");

		REQUIRE(cat.FindById("inconnu") == nullptr);
	}

	void Test_ByCategory()
	{
		AssetCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(kSampleJson, err));
		const auto walls = cat.ByCategory("Wall");
		REQUIRE(walls.size() == 2u);
		const auto doors = cat.ByCategory("Door");
		REQUIRE(doors.size() == 1u);
		REQUIRE(cat.ByCategory("Inexistant").empty());
	}

	void Test_Categories_OrderAndDedup()
	{
		AssetCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(kSampleJson, err));
		const auto cats = cat.Categories();
		// Ordre de première apparition : Wall, Door, Roof (Wall dédupliqué).
		REQUIRE(cats.size() == 3u);
		REQUIRE(cats[0] == "Wall");
		REQUIRE(cats[1] == "Door");
		REQUIRE(cats[2] == "Roof");
	}

	void Test_InvalidEntriesSkipped()
	{
		// Entrée sans gltf ignorée ; count surdimensionné toléré.
		const char* json = R"({
			"assets": {
				"count": 3,
				"0": { "id": "ok", "category": "Wall", "gltf": "meshes/props/ok.gltf" },
				"1": { "id": "sans_gltf", "category": "Wall" },
				"2": { "category": "Wall", "gltf": "meshes/props/sans_id.gltf" }
			}
		})";
		AssetCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(json, err));
		REQUIRE(cat.Size() == 1u); // seule l'entrée 0 est complète
	}

	void Test_EmptyCatalog()
	{
		AssetCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson("{}", err));
		REQUIRE(cat.Size() == 0u);
		REQUIRE(cat.Categories().empty());
	}
}

int main()
{
	Test_ParseJson_Basic();
	Test_ByCategory();
	Test_Categories_OrderAndDedup();
	Test_InvalidEntriesSkipped();
	Test_EmptyCatalog();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[AssetCatalogTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[AssetCatalogTests] all tests passed\n");
	return 0;
}
