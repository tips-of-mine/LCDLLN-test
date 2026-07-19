/// Roadmap-6 (2026-07-19) — Tests unitaires CPU des commandes de placement/
/// déplacement d'instances de layout (PlaceLayoutInstanceCommand /
/// MoveLayoutInstanceCommand). Les foncteurs LayoutPlacementOps sont mockés
/// sur un simple vecteur en RAM (adressage par guid, comme la session).
/// Pur CPU (aucune dépendance ImGui/Vulkan), tourne sous ctest Linux.

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/scene/LayoutPlacementCommands.h"

#include <cstdio>
#include <memory>
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

	using engine::editor::WorldMapEditLayoutInstance;
	using engine::editor::scene::LayoutPlacementOps;
	using engine::editor::scene::MoveLayoutInstanceCommand;
	using engine::editor::scene::PlaceLayoutInstanceCommand;
	using engine::editor::world::CommandStack;

	/// Fabrique les foncteurs mock sur un vecteur d'instances (même sémantique
	/// que WorldEditorSession : add en fin, remove/setPosition par guid).
	LayoutPlacementOps MakeOps(std::vector<WorldMapEditLayoutInstance>* doc)
	{
		LayoutPlacementOps ops;
		ops.add = [doc](const WorldMapEditLayoutInstance& inst) -> bool
		{
			doc->push_back(inst);
			return true;
		};
		ops.removeByGuid = [doc](const std::string& guid) -> bool
		{
			for (size_t i = 0; i < doc->size(); ++i)
			{
				if ((*doc)[i].guid == guid)
				{
					doc->erase(doc->begin() + static_cast<std::ptrdiff_t>(i));
					return true;
				}
			}
			return false;
		};
		ops.setPositionByGuid = [doc](const std::string& guid, double x, double y, double z) -> bool
		{
			for (WorldMapEditLayoutInstance& inst : *doc)
			{
				if (inst.guid == guid)
				{
					inst.worldX = x; inst.worldY = y; inst.worldZ = z;
					return true;
				}
			}
			return false;
		};
		REQUIRE(ops.IsInstalled());
		return ops;
	}

	/// Placement : Execute ajoute (guid conservé), Undo retire, Redo ré-ajoute
	/// avec le MÊME guid.
	void Test_PlaceUndoRedo()
	{
		std::vector<WorldMapEditLayoutInstance> doc;
		WorldMapEditLayoutInstance inst{};
		inst.guid = "inst-4242";
		inst.worldX = 10.0; inst.worldY = 1.0; inst.worldZ = -3.0;
		inst.speciesId = "chene";

		CommandStack stack;
		stack.Push(std::make_unique<PlaceLayoutInstanceCommand>(inst, MakeOps(&doc)));
		REQUIRE(doc.size() == 1u);
		REQUIRE(doc[0].guid == "inst-4242");
		REQUIRE(doc[0].speciesId == "chene");

		stack.Undo();
		REQUIRE(doc.empty());

		stack.Redo();
		REQUIRE(doc.size() == 1u);
		REQUIRE(doc[0].guid == "inst-4242"); // guid stable au Redo
	}

	/// Déplacement : Execute écrit la nouvelle position, Undo restaure
	/// l'ancienne — par guid, insensible à l'ordre du vecteur.
	void Test_MoveUndoRedo()
	{
		std::vector<WorldMapEditLayoutInstance> doc(2);
		doc[0].guid = "a"; doc[0].worldX = 0.0; doc[0].worldY = 0.0; doc[0].worldZ = 0.0;
		doc[1].guid = "b"; doc[1].worldX = 5.0; doc[1].worldY = 1.0; doc[1].worldZ = 5.0;

		CommandStack stack;
		stack.Push(std::make_unique<MoveLayoutInstanceCommand>(
			"b", /*old*/ 5.0, 1.0, 5.0, /*new*/ 8.0, 2.0, -1.0, MakeOps(&doc)));
		REQUIRE(doc[1].worldX == 8.0 && doc[1].worldY == 2.0 && doc[1].worldZ == -1.0);
		REQUIRE(doc[0].worldX == 0.0); // l'autre instance n'est pas touchée

		stack.Undo();
		REQUIRE(doc[1].worldX == 5.0 && doc[1].worldY == 1.0 && doc[1].worldZ == 5.0);

		stack.Redo();
		REQUIRE(doc[1].worldX == 8.0);
	}

	/// Undo d'un placement dont l'instance a déjà disparu (suppression
	/// concurrente) : no-op propre, pas de crash.
	void Test_UndoAfterExternalRemoval()
	{
		std::vector<WorldMapEditLayoutInstance> doc;
		WorldMapEditLayoutInstance inst{};
		inst.guid = "inst-7";

		CommandStack stack;
		stack.Push(std::make_unique<PlaceLayoutInstanceCommand>(inst, MakeOps(&doc)));
		REQUIRE(doc.size() == 1u);
		doc.clear(); // suppression externe (hors commande)
		stack.Undo(); // removeByGuid retourne false : pas de crash
		REQUIRE(doc.empty());
	}
}

int main()
{
	Test_PlaceUndoRedo();
	Test_MoveUndoRedo();
	Test_UndoAfterExternalRemoval();

	if (g_failed == 0)
	{
		std::printf("[PASS] LayoutPlacementCommandsTests\n");
		return 0;
	}
	std::printf("[FAIL] LayoutPlacementCommandsTests: %d failure(s)\n", g_failed);
	return 1;
}
