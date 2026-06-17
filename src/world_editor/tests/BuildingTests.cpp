/// Tests unitaires CPU pour l'entité « Building » (auberge éditable).
///
/// Couvre : binary IO LCBD v1 round-trip (avec pièces), BuildingDocument CRUD,
/// auto-guid, round-trip disque SaveToDisk/LoadFromDisk (chemin namespacé).
///
/// Framework : REQUIRE maison + main monolithique (calque MeshInsertTests).

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

	using engine::world::instances::BuildingInstance;
	using engine::world::instances::BuildingPart;
	using engine::world::instances::LoadBuildingsBin;
	using engine::world::instances::SaveBuildingsBin;
	using engine::editor::world::buildings::BuildingDocument;

	BuildingInstance MakeAuberge()
	{
		BuildingInstance b;
		b.guid = 7u;
		b.displayName = "Auberge";
		b.worldPosition = { 88.0f, 0.0f, 100.0f };
		b.worldYawDeg = 30.0f;
		b.worldScale = 1.0f;
		{
			BuildingPart wall;
			wall.gltfRelativePath = "meshes/props/Wall_Plaster_Straight.gltf";
			wall.localPosition = { -2.0f, 0.0f, 0.0f };
			wall.localEulerDeg = { 0.0f, 90.0f, 0.0f };
			wall.localScale = 1.0f;
			b.parts.push_back(wall);
		}
		{
			BuildingPart roof;
			roof.gltfRelativePath = "meshes/props/Roof_RoundTiles_4x4.gltf";
			roof.localPosition = { 0.0f, 3.5f, 0.0f }; // empilé en hauteur
			roof.localEulerDeg = { 0.0f, 0.0f, 0.0f };
			roof.localScale = 1.25f;
			b.parts.push_back(roof);
		}
		return b;
	}

	/// Test 1 : round-trip binary LCBD v1 (avec pièces + Y empilé).
	void Test_BuildingsBin_RoundTrip()
	{
		std::vector<BuildingInstance> src;
		src.push_back(MakeAuberge());
		{
			BuildingInstance empty;
			empty.guid = 99u;
			empty.displayName = "Cabane vide";
			src.push_back(empty); // bâtiment sans pièce
		}

		const std::vector<uint8_t> bytes = SaveBuildingsBin(src);
		REQUIRE(bytes.size() >= 16u);

		std::vector<BuildingInstance> dst;
		std::string err;
		REQUIRE(LoadBuildingsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.empty());
		REQUIRE(dst.size() == 2u);

		REQUIRE(dst[0].guid == 7u);
		REQUIRE(dst[0].displayName == "Auberge");
		REQUIRE(dst[0].worldPosition.x == 88.0f);
		REQUIRE(dst[0].worldPosition.z == 100.0f);
		REQUIRE(dst[0].worldYawDeg == 30.0f);
		REQUIRE(dst[0].parts.size() == 2u);
		REQUIRE(dst[0].parts[0].gltfRelativePath == "meshes/props/Wall_Plaster_Straight.gltf");
		REQUIRE(dst[0].parts[0].localEulerDeg.y == 90.0f);
		REQUIRE(dst[0].parts[1].localPosition.y == 3.5f);
		REQUIRE(dst[0].parts[1].localScale == 1.25f);

		REQUIRE(dst[1].guid == 99u);
		REQUIRE(dst[1].displayName == "Cabane vide");
		REQUIRE(dst[1].parts.empty());
	}

	/// Test 2 : liste vide → header seul, relecture liste vide sans erreur.
	void Test_BuildingsBin_Empty()
	{
		std::vector<BuildingInstance> src;
		const std::vector<uint8_t> bytes = SaveBuildingsBin(src);
		REQUIRE(bytes.size() == 16u); // header seul
		std::vector<BuildingInstance> dst;
		std::string err;
		REQUIRE(LoadBuildingsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.empty());
	}

	/// Test 3 : magic invalide rejeté.
	void Test_BuildingsBin_BadMagic()
	{
		std::vector<uint8_t> bytes(16u, 0u);
		std::vector<BuildingInstance> dst;
		std::string err;
		REQUIRE(!LoadBuildingsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(!err.empty());
	}

	/// Test 4 : BuildingDocument CRUD + callbacks.
	void Test_BuildingDocument_Crud()
	{
		BuildingDocument doc;
		int added = 0, updated = 0, removed = 0;
		doc.SetOnAdded([&](const BuildingInstance&) { ++added; });
		doc.SetOnUpdated([&](const BuildingInstance&) { ++updated; });
		doc.SetOnRemoved([&](uint64_t) { ++removed; });

		const uint64_t guid = doc.Add(MakeAuberge());
		REQUIRE(guid == 7u); // guid explicite préservé
		REQUIRE(doc.Size() == 1u);
		REQUIRE(added == 1);

		const BuildingInstance* found = doc.GetByGuid(guid);
		REQUIRE(found != nullptr);
		REQUIRE(found->displayName == "Auberge");

		BuildingInstance mod = *found;
		mod.worldYawDeg = 180.0f;
		REQUIRE(doc.Update(guid, mod));
		REQUIRE(updated == 1);
		REQUIRE(doc.GetByGuid(guid)->worldYawDeg == 180.0f);

		BuildingInstance* mut = doc.MutableByGuid(guid);
		REQUIRE(mut != nullptr);
		mut->worldScale = 2.0f;
		REQUIRE(doc.GetByGuid(guid)->worldScale == 2.0f);

		REQUIRE(doc.Remove(guid));
		REQUIRE(removed == 1);
		REQUIRE(doc.Size() == 0u);
		REQUIRE(!doc.Remove(guid)); // double-remove no-op
	}

	/// Test 5 : guid auto pour `guid == 0`.
	void Test_BuildingDocument_AutoGuid()
	{
		BuildingDocument doc;
		BuildingInstance a; a.displayName = "a";
		BuildingInstance b; b.displayName = "b";
		const uint64_t ga = doc.Add(a);
		const uint64_t gb = doc.Add(b);
		REQUIRE(ga > 0u);
		REQUIRE(gb > 0u);
		REQUIRE(ga != gb);
	}

	/// Test 6 : round-trip disque (chemin namespacé zone_<id>).
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
		const BuildingInstance* b = reloaded.GetByGuid(7u);
		REQUIRE(b != nullptr);
		REQUIRE(b->parts.size() == 2u);
		REQUIRE(b->parts[1].localPosition.y == 3.5f);

		// Nouvel Add ne doit pas réutiliser le guid 7 (compteur initialisé au max).
		const uint64_t next = reloaded.Add(BuildingInstance{});
		REQUIRE(next > 7u);

		fs::remove_all(tmp, ec);
	}

	/// Test 7 : LoadFromDisk sur zone absente → doc vide sans erreur.
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
