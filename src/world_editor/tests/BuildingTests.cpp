/// Tests unitaires CPU pour les PLACEMENTS de bâtiments (références carte).
///
/// Couvre : binary IO LCBD v1 round-trip (références type+variante), bad magic,
/// BuildingDocument CRUD, auto-guid, round-trip disque namespacé.
/// (La bibliothèque de types est testée dans BuildingTemplateLibraryTests.cpp.)

#include "src/client/world/instances/Buildings.h"
#include "src/shared/core/Config.h"
#include "src/world_editor/buildings/BuildingDocument.h"

#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::instances::BuildingPlacement;
	using engine::world::instances::LoadBuildingsBin;
	using engine::world::instances::SaveBuildingsBin;
	using engine::editor::world::buildings::BuildingDocument;

	BuildingPlacement MakeAuberge()
	{
		BuildingPlacement pl;
		pl.guid = 7u;
		pl.templateType = "tavern";
		pl.variantId = "auberge_terrasse";
		pl.displayName = "Auberge";
		pl.worldPosition = { 88.0f, 0.0f, 100.0f };
		pl.worldYawDeg = 30.0f;
		pl.worldScale = 1.0f;
		return pl;
	}

	void Test_BuildingsBin_RoundTrip()
	{
		std::vector<BuildingPlacement> src;
		src.push_back(MakeAuberge());
		{
			BuildingPlacement house;
			house.guid = 99u;
			house.templateType = "house";
			house.variantId = "maison_01";
			house.worldPosition = { 10.0f, 0.0f, 20.0f };
			src.push_back(house);
		}

		const std::vector<uint8_t> bytes = SaveBuildingsBin(src);
		REQUIRE(bytes.size() >= 16u);

		std::vector<BuildingPlacement> dst;
		std::string err;
		REQUIRE(LoadBuildingsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.empty());
		REQUIRE(dst.size() == 2u);

		REQUIRE(dst[0].guid == 7u);
		REQUIRE(dst[0].templateType == "tavern");
		REQUIRE(dst[0].variantId == "auberge_terrasse");
		REQUIRE(dst[0].displayName == "Auberge");
		REQUIRE(dst[0].worldPosition.x == 88.0f);
		REQUIRE(dst[0].worldPosition.z == 100.0f);
		REQUIRE(dst[0].worldYawDeg == 30.0f);

		REQUIRE(dst[1].guid == 99u);
		REQUIRE(dst[1].templateType == "house");
		REQUIRE(dst[1].variantId == "maison_01");
	}

	void Test_BuildingsBin_Empty()
	{
		std::vector<BuildingPlacement> src;
		const std::vector<uint8_t> bytes = SaveBuildingsBin(src);
		REQUIRE(bytes.size() == 16u); // header seul
		std::vector<BuildingPlacement> dst;
		std::string err;
		REQUIRE(LoadBuildingsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.empty());
	}

	void Test_BuildingsBin_BadMagic()
	{
		std::vector<uint8_t> bytes(16u, 0u);
		std::vector<BuildingPlacement> dst;
		std::string err;
		REQUIRE(!LoadBuildingsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(!err.empty());
	}

	void Test_BuildingDocument_Crud()
	{
		BuildingDocument doc;
		int added = 0, updated = 0, removed = 0;
		doc.SetOnAdded([&](const BuildingPlacement&) { ++added; });
		doc.SetOnUpdated([&](const BuildingPlacement&) { ++updated; });
		doc.SetOnRemoved([&](uint64_t) { ++removed; });

		const uint64_t guid = doc.Add(MakeAuberge());
		REQUIRE(guid == 7u);
		REQUIRE(doc.Size() == 1u);
		REQUIRE(added == 1);

		const BuildingPlacement* found = doc.GetByGuid(guid);
		REQUIRE(found != nullptr);
		REQUIRE(found->variantId == "auberge_terrasse");

		BuildingPlacement mod = *found;
		mod.worldYawDeg = 180.0f;
		REQUIRE(doc.Update(guid, mod));
		REQUIRE(updated == 1);
		REQUIRE(doc.GetByGuid(guid)->worldYawDeg == 180.0f);

		BuildingPlacement* mut = doc.MutableByGuid(guid);
		REQUIRE(mut != nullptr);
		mut->worldScale = 2.0f;
		REQUIRE(doc.GetByGuid(guid)->worldScale == 2.0f);

		REQUIRE(doc.Remove(guid));
		REQUIRE(removed == 1);
		REQUIRE(doc.Size() == 0u);
		REQUIRE(!doc.Remove(guid));
	}

	void Test_BuildingDocument_AutoGuid()
	{
		BuildingDocument doc;
		BuildingPlacement a; a.templateType = "house"; a.variantId = "a";
		BuildingPlacement b; b.templateType = "house"; b.variantId = "b";
		const uint64_t ga = doc.Add(a);
		const uint64_t gb = doc.Add(b);
		REQUIRE(ga > 0u);
		REQUIRE(gb > 0u);
		REQUIRE(ga != gb);
	}

	void Test_BuildingDocument_DiskRoundTrip()
	{
		namespace fs = std::filesystem;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_building_test_content";
		std::error_code ec;
		fs::remove_all(tmp, ec);

		engine::core::Config cfg;
		cfg.SetValue("paths.content", std::string(tmp.string()));

		BuildingDocument doc;
		doc.SetZoneId("test_zone");
		doc.Add(MakeAuberge());

		std::string err;
		REQUIRE(doc.SaveToDisk(cfg, err));
		REQUIRE(err.empty());
		REQUIRE(!doc.IsDirty());
		REQUIRE(fs::exists(tmp / "instances" / "zone_test_zone" / "buildings.bin"));

		BuildingDocument reloaded;
		reloaded.SetZoneId("test_zone");
		REQUIRE(reloaded.LoadFromDisk(cfg, err));
		REQUIRE(reloaded.Size() == 1u);
		const BuildingPlacement* b = reloaded.GetByGuid(7u);
		REQUIRE(b != nullptr);
		REQUIRE(b->templateType == "tavern");

		const uint64_t next = reloaded.Add(BuildingPlacement{});
		REQUIRE(next > 7u);

		fs::remove_all(tmp, ec);
	}

	void Test_BuildingDocument_MissingFile()
	{
		namespace fs = std::filesystem;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_building_test_absent";
		std::error_code ec;
		fs::remove_all(tmp, ec);

		engine::core::Config cfg;
		cfg.SetValue("paths.content", std::string(tmp.string()));
		BuildingDocument doc;
		doc.SetZoneId("nope");
		std::string err;
		REQUIRE(doc.LoadFromDisk(cfg, err));
		REQUIRE(doc.Size() == 0u);
	}
}

int main()
{
	Test_BuildingsBin_RoundTrip();
	Test_BuildingsBin_Empty();
	Test_BuildingsBin_BadMagic();
	Test_BuildingDocument_Crud();
	Test_BuildingDocument_AutoGuid();
	Test_BuildingDocument_DiskRoundTrip();
	Test_BuildingDocument_MissingFile();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[BuildingTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[BuildingTests] all tests passed\n");
	return 0;
}
