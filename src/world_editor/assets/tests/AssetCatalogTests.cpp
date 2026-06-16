// Auberge éditable (T1) — Tests AssetCatalog : catégorisation + scan disque.
// Headless. Lié à engine_core.

#include "src/world_editor/assets/AssetCatalog.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace engine::editor::world::assets;

namespace
{
	int g_failed = 0;
#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	void Touch(const std::filesystem::path& p)
	{
		std::ofstream f(p); f << "{}";
	}

	void Test_Categorize()
	{
		REQUIRE(CategorizeAsset("Wall_Plaster_Straight.gltf") == AssetCategory::Wall);
		REQUIRE(CategorizeAsset("Door_2_Round.gltf") == AssetCategory::Door);
		REQUIRE(CategorizeAsset("Roof_RoundTiles_6x8.gltf") == AssetCategory::Roof);
		REQUIRE(CategorizeAsset("Table_Large.gltf") == AssetCategory::Furniture);
		REQUIRE(CategorizeAsset("Barrel.gltf") == AssetCategory::Container);
		REQUIRE(CategorizeAsset("Lantern_Wall.gltf") == AssetCategory::Lighting);
		REQUIRE(CategorizeAsset("Zorglub_X.gltf") == AssetCategory::Unknown);
	}

	void Test_Scan()
	{
		namespace fs = std::filesystem;
		const fs::path dir = fs::temp_directory_path() / "lcdlln_asset_scan_test";
		fs::remove_all(dir);
		fs::create_directories(dir);
		Touch(dir / "Wall_Plaster_Straight.gltf");
		Touch(dir / "Door_2_Round.gltf");
		Touch(dir / "Wall_Plaster_Straight.bin");
		Touch(dir / "notes.txt");

		auto entries = ScanPropAssets(dir.string(), "meshes/props/");
		REQUIRE(entries.size() == 2);
		REQUIRE(entries[0].category == AssetCategory::Wall);
		REQUIRE(entries[0].relativePath == "meshes/props/Wall_Plaster_Straight.gltf");
		REQUIRE(entries[1].category == AssetCategory::Door);
		fs::remove_all(dir);
	}

	void Test_Scan_MissingDir()
	{
		auto entries = ScanPropAssets("/nonexistent/dir/xyz", "meshes/props/");
		REQUIRE(entries.empty());
	}
}

int main()
{
	Test_Categorize();
	Test_Scan();
	Test_Scan_MissingDir();
	if (g_failed == 0) std::fprintf(stderr, "[OK] AssetCatalogTests\n");
	else std::fprintf(stderr, "[FAIL] AssetCatalogTests: %d\n", g_failed);
	return g_failed;
}
