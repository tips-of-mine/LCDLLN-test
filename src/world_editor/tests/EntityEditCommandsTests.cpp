/// Tests unitaires CPU pour DeleteEntityCommand / DuplicateEntityCommand
/// (lot 5, 2026-07-18). Simulent les foncteurs EntityEditOps de l'Engine sur
/// un vecteur de layout instances en mémoire : Execute/Undo/Redo, réinsertion
/// au rang d'origine, duplication à guid neuf, intégration CommandStack, et
/// no-op propre quand l'entité n'existe pas. Pur CPU, ctest (non gate WIN32).

#include "src/world_editor/scene/DeleteEntityCommand.h"
#include "src/world_editor/scene/DuplicateEntityCommand.h"
#include "src/world_editor/core/CommandStack.h"

#include <cstddef>
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

	using namespace engine::editor::world;
	using engine::editor::scene::EntityEditOps;
	using engine::editor::scene::EntityId;
	using engine::editor::scene::EntityKind;
	using engine::editor::scene::EntitySnapshot;
	using engine::editor::WorldMapEditLayoutInstance;

	/// Fabrique une instance de layout minimale (guid + position X).
	WorldMapEditLayoutInstance MakeInst(const char* guid, double x)
	{
		WorldMapEditLayoutInstance i;
		i.guid = guid;
		i.gltfContentRelativePath = "zones/zone_test/rock.gltf";
		i.worldX = x;
		return i;
	}

	/// Construit des EntityEditOps factices opérant sur \p insts, avec la même
	/// sémantique que les foncteurs Engine : capture par index, remove par
	/// guid, restore au rang d'origine (borné), duplicate = guid neuf + +1,5 m.
	EntityEditOps MakeLayoutOps(std::vector<WorldMapEditLayoutInstance>& insts)
	{
		EntityEditOps ops;
		ops.capture = [&insts](EntityId id, EntitySnapshot& out) -> bool
		{
			if (id.kind != EntityKind::LayoutInstance || id.index >= insts.size()) return false;
			out.kind = id.kind;
			out.layout = insts[id.index];
			out.layoutIndex = id.index;
			return true;
		};
		ops.remove = [&insts](const EntitySnapshot& s) -> bool
		{
			if (s.kind != EntityKind::LayoutInstance) return false;
			for (size_t i = 0; i < insts.size(); ++i)
			{
				if (insts[i].guid == s.layout.guid)
				{
					insts.erase(insts.begin() + static_cast<ptrdiff_t>(i));
					return true;
				}
			}
			return false;
		};
		ops.restore = [&insts](const EntitySnapshot& s) -> bool
		{
			if (s.kind != EntityKind::LayoutInstance) return false;
			const size_t at = s.layoutIndex < insts.size() ? s.layoutIndex : insts.size();
			insts.insert(insts.begin() + static_cast<ptrdiff_t>(at), s.layout);
			return true;
		};
		ops.duplicate = [&insts](const EntitySnapshot& src, EntitySnapshot& outCopy) -> bool
		{
			if (src.kind != EntityKind::LayoutInstance) return false;
			static int s_seq = 1;
			outCopy = src;
			outCopy.layout.guid = std::string("dup_") + std::to_string(s_seq++);
			outCopy.layout.worldX += 1.5;
			outCopy.layoutIndex = static_cast<uint32_t>(insts.size());
			insts.push_back(outCopy.layout);
			return true;
		};
		return ops;
	}

	void Test_DeleteExecuteUndoRedo()
	{
		std::vector<WorldMapEditLayoutInstance> insts{
			MakeInst("a", 0.0), MakeInst("b", 1.0), MakeInst("c", 2.0) };
		EntityEditOps ops = MakeLayoutOps(insts);

		DeleteEntityCommand cmd(EntityId{EntityKind::LayoutInstance, 1u}, ops);
		cmd.Execute();
		REQUIRE(insts.size() == 2u);
		REQUIRE(insts[0].guid == "a");
		REQUIRE(insts[1].guid == "c");

		// Undo : "b" revient à SON rang d'origine (index 1), pas en fin.
		cmd.Undo();
		REQUIRE(insts.size() == 3u);
		REQUIRE(insts[1].guid == "b");

		// Redo : re-suppression par guid (les index ont pu bouger entre-temps).
		cmd.Execute();
		REQUIRE(insts.size() == 2u);
		REQUIRE(insts[1].guid == "c");
	}

	void Test_DeleteInvalidIndexIsNoop()
	{
		std::vector<WorldMapEditLayoutInstance> insts{ MakeInst("a", 0.0) };
		EntityEditOps ops = MakeLayoutOps(insts);

		DeleteEntityCommand cmd(EntityId{EntityKind::LayoutInstance, 7u}, ops);
		cmd.Execute();
		REQUIRE(insts.size() == 1u); // rien supprimé
		cmd.Undo();
		REQUIRE(insts.size() == 1u); // rien réinséré non plus
	}

	void Test_DuplicateExecuteUndoRedo()
	{
		std::vector<WorldMapEditLayoutInstance> insts{ MakeInst("a", 10.0) };
		EntityEditOps ops = MakeLayoutOps(insts);

		DuplicateEntityCommand cmd(EntityId{EntityKind::LayoutInstance, 0u}, ops);
		cmd.Execute();
		REQUIRE(insts.size() == 2u);
		REQUIRE(insts[1].guid != "a");            // guid neuf
		REQUIRE(insts[1].worldX == 11.5);          // décalage +1,5 m
		const std::string copyGuid = insts[1].guid;

		cmd.Undo();
		REQUIRE(insts.size() == 1u);
		REQUIRE(insts[0].guid == "a");

		// Redo : la MÊME copie revient (guid conservé, pas un 2e clone).
		cmd.Execute();
		REQUIRE(insts.size() == 2u);
		REQUIRE(insts[1].guid == copyGuid);
	}

	void Test_ViaCommandStack()
	{
		std::vector<WorldMapEditLayoutInstance> insts{
			MakeInst("a", 0.0), MakeInst("b", 1.0) };
		EntityEditOps ops = MakeLayoutOps(insts);
		CommandStack stack;

		stack.Push(std::make_unique<DuplicateEntityCommand>(
			EntityId{EntityKind::LayoutInstance, 0u}, ops));
		REQUIRE(insts.size() == 3u);
		stack.Push(std::make_unique<DeleteEntityCommand>(
			EntityId{EntityKind::LayoutInstance, 1u}, ops)); // supprime "b"
		REQUIRE(insts.size() == 2u);
		REQUIRE(stack.UndoSize() == 2u); // pas de coalescing entre ces commandes

		stack.Undo(); // "b" revient
		REQUIRE(insts.size() == 3u);
		REQUIRE(insts[1].guid == "b");
		stack.Undo(); // la copie disparaît
		REQUIRE(insts.size() == 2u);
		stack.Redo();
		stack.Redo();
		REQUIRE(insts.size() == 2u);
		REQUIRE(insts[0].guid == "a");
	}
}

int main()
{
	Test_DeleteExecuteUndoRedo();
	Test_DeleteInvalidIndexIsNoop();
	Test_DuplicateExecuteUndoRedo();
	Test_ViaCommandStack();

	if (g_failed == 0)
	{
		std::printf("[PASS] EntityEditCommandsTests\n");
		return 0;
	}
	std::printf("[FAIL] EntityEditCommandsTests: %d failure(s)\n", g_failed);
	return 1;
}
