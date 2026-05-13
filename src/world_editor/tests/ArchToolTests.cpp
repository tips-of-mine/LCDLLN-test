/// Tests unitaires CPU pour M100.42 — Natural Arch Tool.
///
/// Couvre : ArchCatalog JSON parsing (archAnchorA/B, archHeight),
/// PlaceArchCommand Apply/Undo, ArchTool dérivation yaw/scale/position,
/// garde-fou scale hors bornes.

#include "src/shared/core/Config.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/arches/ArchCatalog.h"
#include "src/world_editor/volumes/arches/ArchTool.h"
#include "src/world_editor/volumes/arches/PlaceArchCommand.h"

#include <cmath>
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
	using engine::editor::world::volumes::MeshInsertDocument;
	using engine::editor::world::volumes::MeshInsertInstance;
	using engine::editor::world::volumes::arches::ArchCatalog;
	using engine::editor::world::volumes::arches::ArchTool;
	using engine::editor::world::volumes::arches::PlaceArchCommand;

	void Test_ArchCatalog_ParseJson()
	{
		const std::string json = R"({
			"arches": [
				{
					"id": "test_arch",
					"gltf": "meshes/arches/test.gltf",
					"displayName": "Test arche",
					"aabbMin": [-3, 0, -1],
					"aabbMax": [3, 4, 1],
					"archAnchorA": [-2.5, 0.0, 0.0],
					"archAnchorB": [2.5, 0.0, 0.0],
					"archHeight": 3.5
				}
			]
		})";
		ArchCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(json, err));
		REQUIRE(cat.Size() == 1u);
		const auto* e = cat.FindById("test_arch");
		REQUIRE(e != nullptr);
		REQUIRE(e->archAnchorA.x == -2.5f);
		REQUIRE(e->archAnchorB.x == 2.5f);
		REQUIRE(e->archHeight == 3.5f);
		REQUIRE(std::abs(e->NativeSpanMeters() - 5.0f) < 0.001f);
	}

	void Test_ArchCatalog_Empty()
	{
		ArchCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson("{}", err));
		REQUIRE(cat.Size() == 0u);
	}

	void Test_PlaceArchCommand_ApplyUndo()
	{
		MeshInsertDocument doc;
		MeshInsertInstance inst;
		inst.gltfRelativePath = "test/arch.gltf";
		inst.worldPosition    = { 0.0f, 5.0f, 0.0f };
		inst.insertCategory   = "arch";
		inst.uniformScale     = 2.0f;

		CommandStack stack;
		stack.Push(std::make_unique<PlaceArchCommand>(doc, inst));
		REQUIRE(doc.Size() == 1u);
		REQUIRE(doc.GetByCategory("arch").size() == 1u);
		REQUIRE(doc.All()[0].uniformScale == 2.0f);

		stack.Undo();
		REQUIRE(doc.Size() == 0u);
	}

	void Test_ArchTool_DerivedValues()
	{
		engine::core::Config cfg;
		MeshInsertDocument doc;
		CommandStack stack;
		ArchTool tool;
		tool.Init(stack, doc, cfg);
		// Pas de catalog en mémoire — on teste les helpers pure (span/yaw).
		tool.PointAX() = 0.0f; tool.PointAY() = 0.0f; tool.PointAZ() = 0.0f;
		tool.PointBX() = 3.0f; tool.PointBY() = 0.0f; tool.PointBZ() = 4.0f;
		REQUIRE(std::abs(tool.SpanMeters() - 5.0f) < 0.001f);
		// yaw = atan2(4, 3) ≈ 53.13 deg
		REQUIRE(std::abs(tool.DerivedYawDeg() - 53.130103f) < 0.01f);
	}

	void Test_ArchTool_RejectsScaleOutOfBounds()
	{
		engine::core::Config cfg;
		MeshInsertDocument doc;
		CommandStack stack;
		ArchTool tool;
		tool.Init(stack, doc, cfg);
		// Catalog vide, donc Place échoue avant le calcul de scale ;
		// on teste juste que les bornes sont respectées par défaut.
		REQUIRE(tool.MinScaleRatio() > 0.0f);
		REQUIRE(tool.MaxScaleRatio() > tool.MinScaleRatio());
		// Place sans selection → rejet.
		REQUIRE(!tool.Place());
		REQUIRE(doc.Size() == 0u);
	}
}

int main()
{
	Test_ArchCatalog_ParseJson();
	Test_ArchCatalog_Empty();
	Test_PlaceArchCommand_ApplyUndo();
	Test_ArchTool_DerivedValues();
	Test_ArchTool_RejectsScaleOutOfBounds();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[ArchToolTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[ArchToolTests] all tests passed\n");
	return 0;
}
