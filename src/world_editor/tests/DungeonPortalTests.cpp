/// Tests unitaires CPU pour M100.43 — Dungeon Portal System.
///
/// Couvre : LCDP v1 binary round-trip, DungeonPortalDocument CRUD,
/// DungeonCatalog JSON parsing, PlaceDungeonPortalCommand Apply/Undo,
/// DungeonPortalTool gating (sans selection / difficulty hors range
/// catalog).

#include "src/shared/core/Config.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/dungeons/DungeonCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalIo.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalTool.h"
#include "src/world_editor/volumes/dungeons/PlaceDungeonPortalCommand.h"

#include <cstdio>
#include <memory>
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

	using engine::editor::world::CommandStack;
	using engine::editor::world::volumes::dungeons::DungeonCatalog;
	using engine::editor::world::volumes::dungeons::DungeonPortalDocument;
	using engine::editor::world::volumes::dungeons::DungeonPortalInstance;
	using engine::editor::world::volumes::dungeons::DungeonPortalTool;
	using engine::editor::world::volumes::dungeons::LoadDungeonPortalsBin;
	using engine::editor::world::volumes::dungeons::PlaceDungeonPortalCommand;
	using engine::editor::world::volumes::dungeons::SaveDungeonPortalsBin;

	/// LCDP v1 round-trip avec tous les champs.
	void Test_LCDP_RoundTrip()
	{
		std::vector<DungeonPortalInstance> src;
		DungeonPortalInstance a;
		a.guid              = 7u;
		a.dungeonTemplateId = "dungeon_crypt";
		a.displayName       = "Crypte test";
		a.decorativeMeshPath = "meshes/dungeons/decorative/x.gltf";
		a.worldPosition    = { 10.0f, 20.0f, 30.0f };
		a.eulerRotationDeg = { 0.0f, 90.0f, 0.0f };
		a.triggerRadius    = 4.5f;
		a.requiredLevel    = 25u;
		a.minDifficulty    = 1u;
		a.maxDifficulty    = 3u;
		a.isOneShot           = true;
		a.persistsAcrossLogin = true;
		src.push_back(a);

		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveDungeonPortalsBin(src, bytes, err));
		REQUIRE(err.empty());

		std::vector<DungeonPortalInstance> dst;
		REQUIRE(LoadDungeonPortalsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.size() == 1u);
		REQUIRE(dst[0].guid == 7u);
		REQUIRE(dst[0].dungeonTemplateId == "dungeon_crypt");
		REQUIRE(dst[0].triggerRadius == 4.5f);
		REQUIRE(dst[0].requiredLevel == 25u);
		REQUIRE(dst[0].minDifficulty == 1u);
		REQUIRE(dst[0].maxDifficulty == 3u);
		REQUIRE(dst[0].isOneShot == true);
		REQUIRE(dst[0].persistsAcrossLogin == true);
	}

	void Test_DungeonPortalDocument_CRUD()
	{
		DungeonPortalDocument doc;
		DungeonPortalInstance inst;
		inst.dungeonTemplateId = "dungeon_test";
		inst.requiredLevel     = 10u;

		const uint64_t guid = doc.Add(inst);
		REQUIRE(guid > 0u);
		REQUIRE(doc.Size() == 1u);
		REQUIRE(doc.GetByGuid(guid) != nullptr);
		REQUIRE(doc.GetByGuid(guid)->requiredLevel == 10u);

		DungeonPortalInstance updated = *doc.GetByGuid(guid);
		updated.requiredLevel = 30u;
		REQUIRE(doc.Update(guid, updated));
		REQUIRE(doc.GetByGuid(guid)->requiredLevel == 30u);

		REQUIRE(doc.Remove(guid));
		REQUIRE(doc.Size() == 0u);
		REQUIRE(!doc.Remove(guid));
	}

	void Test_DungeonCatalog_ParseJson()
	{
		const std::string json = R"({
			"dungeons": [
				{
					"id": "dungeon_test",
					"displayName": "Test",
					"description": "Desc test",
					"requiredLevel": 20,
					"minDifficulty": 1,
					"maxDifficulty": 3
				}
			]
		})";
		DungeonCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(json, err));
		REQUIRE(cat.Size() == 1u);
		const auto* e = cat.FindById("dungeon_test");
		REQUIRE(e != nullptr);
		REQUIRE(e->displayName == "Test");
		REQUIRE(e->requiredLevel == 20u);
		REQUIRE(e->minDifficulty == 1u);
		REQUIRE(e->maxDifficulty == 3u);
	}

	void Test_PlaceDungeonPortalCommand_ApplyUndo()
	{
		DungeonPortalDocument doc;
		DungeonPortalInstance inst;
		inst.dungeonTemplateId = "dungeon_test";
		inst.worldPosition = { 1.0f, 2.0f, 3.0f };
		inst.triggerRadius = 5.0f;

		CommandStack stack;
		stack.Push(std::make_unique<PlaceDungeonPortalCommand>(doc, inst));
		REQUIRE(doc.Size() == 1u);
		REQUIRE(doc.All()[0].triggerRadius == 5.0f);

		stack.Undo();
		REQUIRE(doc.Size() == 0u);
	}

	void Test_DungeonPortalTool_RejectsWithoutSelection()
	{
		engine::core::Config cfg;
		DungeonPortalDocument doc;
		CommandStack stack;
		DungeonPortalTool tool;
		tool.Init(stack, doc, cfg);
		REQUIRE(!tool.Place());
		REQUIRE(doc.Size() == 0u);
	}

	void Test_DungeonPortalTool_RejectsInvalidDifficultyRange()
	{
		engine::core::Config cfg;
		DungeonPortalDocument doc;
		CommandStack stack;
		DungeonPortalTool tool;
		tool.Init(stack, doc, cfg);
		tool.SelectByTemplateId("anything");
		// catalog vide → Place rejette quoi qu'il arrive (FindById nullptr).
		tool.MinDifficulty() = 3u;
		tool.MaxDifficulty() = 1u; // incohérent
		REQUIRE(!tool.Place());
		REQUIRE(doc.Size() == 0u);
	}

	void Test_LCDP_BadMagic()
	{
		std::vector<uint8_t> bytes(16u, 0x00); // header zéros = bad magic
		std::vector<DungeonPortalInstance> dst;
		std::string err;
		REQUIRE(!LoadDungeonPortalsBin(std::span<const uint8_t>(bytes), dst, err));
	}
}

int main()
{
	Test_LCDP_RoundTrip();
	Test_DungeonPortalDocument_CRUD();
	Test_DungeonCatalog_ParseJson();
	Test_PlaceDungeonPortalCommand_ApplyUndo();
	Test_DungeonPortalTool_RejectsWithoutSelection();
	Test_DungeonPortalTool_RejectsInvalidDifficultyRange();
	Test_LCDP_BadMagic();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[DungeonPortalTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[DungeonPortalTests] all tests passed\n");
	return 0;
}
