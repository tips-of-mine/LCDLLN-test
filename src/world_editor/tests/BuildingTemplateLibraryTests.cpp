/// Tests unitaires CPU pour BuildingTemplateLibrary (bibliothèque de types).
///
/// Couvre : parse JSON d'un type + variantes + pièces, Resolve(type, variante),
/// FindType, SaveVariant (écriture fichier <type>.json + relecture + upsert).

#include "src/client/world/instances/BuildingTemplateLibrary.h"

#include <cstdio>
#include <filesystem>
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

	bool Near(float a, float b, float eps = 1e-4f) { return (a - b < eps) && (b - a < eps); }

	using engine::world::instances::BuildingTemplateLibrary;
	using engine::world::instances::BuildingVariant;
	using engine::world::instances::BuildingPart;

	const char* kTavernJson = R"({
		"type": "tavern",
		"displayName": "Taverne / Auberge",
		"variants": {
			"count": 1,
			"0": {
				"id": "auberge_terrasse",
				"displayName": "Auberge - terrasse",
				"parts": {
					"count": 2,
					"0": { "mesh": "meshes/props/Table_Large.gltf",
					       "x": 2.5, "y": 0.0, "z": 0.6, "ry": 0.0,
					       "scale": 1.0, "solid": true, "collision_radius": 0.9 },
					"1": { "mesh": "meshes/props/Roof_RoundTiles_4x4.gltf",
					       "x": 0.0, "y": 3.5, "z": 0.0,
					       "scale": 1.25, "solid": false, "collision_radius": 0.0 }
				}
			}
		}
	})";

	void Test_ParseAndResolve()
	{
		BuildingTemplateLibrary lib;
		std::string err;
		REQUIRE(lib.LoadTemplateFromJson(kTavernJson, err));
		REQUIRE(err.empty());
		REQUIRE(lib.Size() == 1u);

		const auto* t = lib.FindType("tavern");
		REQUIRE(t != nullptr);
		REQUIRE(t->displayName == "Taverne / Auberge");
		REQUIRE(t->variants.size() == 1u);

		const BuildingVariant* v = lib.Resolve("tavern", "auberge_terrasse");
		REQUIRE(v != nullptr);
		REQUIRE(v->parts.size() == 2u);
		REQUIRE(v->parts[0].gltfRelativePath == "meshes/props/Table_Large.gltf");
		REQUIRE(Near(v->parts[0].localPosition.x, 2.5f));
		REQUIRE(v->parts[0].solid == true);
		REQUIRE(Near(v->parts[0].collisionRadius, 0.9f));
		REQUIRE(Near(v->parts[1].localPosition.y, 3.5f));
		REQUIRE(Near(v->parts[1].localScale, 1.25f));
		REQUIRE(v->parts[1].solid == false);

		// Résolutions négatives.
		REQUIRE(lib.Resolve("tavern", "inconnu") == nullptr);
		REQUIRE(lib.Resolve("house", "auberge_terrasse") == nullptr);
	}

	void Test_UpsertReplacesType()
	{
		BuildingTemplateLibrary lib;
		std::string err;
		REQUIRE(lib.LoadTemplateFromJson(kTavernJson, err));
		// Recharger le même type doit le remplacer (pas dupliquer).
		REQUIRE(lib.LoadTemplateFromJson(kTavernJson, err));
		REQUIRE(lib.Size() == 1u);
	}

	void Test_SaveVariant_RoundTrip()
	{
		namespace fs = std::filesystem;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_building_lib_test";
		std::error_code ec;
		fs::remove_all(tmp, ec);

		BuildingVariant var;
		var.id = "maison_simple";
		var.displayName = "Maison simple";
		{
			BuildingPart wall;
			wall.gltfRelativePath = "meshes/props/Wall_Plaster_Straight.gltf";
			wall.localPosition = { -2.0f, 0.0f, 0.0f };
			wall.localEulerDeg = { 0.0f, 90.0f, 0.0f };
			wall.localScale = 1.0f;
			wall.solid = true;
			wall.collisionRadius = 1.5f;
			var.parts.push_back(wall);
		}

		BuildingTemplateLibrary lib;
		std::string err;
		REQUIRE(lib.SaveVariant(tmp.string(), "house", "Maisons", var, err));
		REQUIRE(err.empty());
		REQUIRE(fs::exists(tmp / "buildings" / "templates" / "house.json"));

		// Relecture depuis le dossier.
		BuildingTemplateLibrary reloaded;
		REQUIRE(reloaded.LoadFromContent(tmp.string(), err));
		const BuildingVariant* v = reloaded.Resolve("house", "maison_simple");
		REQUIRE(v != nullptr);
		REQUIRE(v->parts.size() == 1u);
		REQUIRE(Near(v->parts[0].localEulerDeg.y, 90.0f));
		REQUIRE(Near(v->parts[0].collisionRadius, 1.5f));

		// Ajouter une 2e variante au MÊME type → upsert, fichier contient 2.
		BuildingVariant var2;
		var2.id = "maison_double";
		var2.displayName = "Maison double";
		REQUIRE(lib.SaveVariant(tmp.string(), "house", "Maisons", var2, err));
		BuildingTemplateLibrary reloaded2;
		REQUIRE(reloaded2.LoadFromContent(tmp.string(), err));
		const auto* t = reloaded2.FindType("house");
		REQUIRE(t != nullptr);
		if (t) REQUIRE(t->variants.size() == 2u);

		// Remplacer une variante existante (même id) → toujours 2.
		var.displayName = "Maison simple v2";
		REQUIRE(lib.SaveVariant(tmp.string(), "house", "Maisons", var, err));
		BuildingTemplateLibrary reloaded3;
		REQUIRE(reloaded3.LoadFromContent(tmp.string(), err));
		const auto* t3 = reloaded3.FindType("house");
		REQUIRE(t3 != nullptr);
		if (t3) REQUIRE(t3->variants.size() == 2u);

		fs::remove_all(tmp, ec);
	}

	void Test_LoadFromContent_MissingFolder()
	{
		namespace fs = std::filesystem;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_building_lib_absent";
		std::error_code ec;
		fs::remove_all(tmp, ec);
		BuildingTemplateLibrary lib;
		std::string err;
		REQUIRE(lib.LoadFromContent(tmp.string(), err)); // dossier absent => true, vide
		REQUIRE(lib.Size() == 0u);
	}
}

int main()
{
	Test_ParseAndResolve();
	Test_UpsertReplacesType();
	Test_SaveVariant_RoundTrip();
	Test_LoadFromContent_MissingFolder();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[BuildingTemplateLibraryTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[BuildingTemplateLibraryTests] all tests passed\n");
	return 0;
}
