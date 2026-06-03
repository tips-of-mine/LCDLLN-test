/// Tests unitaires CPU pour SetEntityTransformCommand (sous-projet 1, bloc D).
/// Vérifie Execute/Undo (application du nouveau / ancien transform), la fusion
/// (TryMerge) des éditions consécutives d'une même entité, et l'intégration
/// avec CommandStack (un seul élément d'historique par geste). Pur CPU, ctest.

#include "src/world_editor/inspector/SetEntityTransformCommand.h"
#include "src/world_editor/core/CommandStack.h"

#include <cstdio>
#include <memory>

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
	using engine::editor::scene::EntityId;
	using engine::editor::scene::EntityKind;
	using engine::editor::scene::EntityTransform;

	EntityTransform MakeT(float x)
	{
		EntityTransform t;
		t.position.x = x;
		t.uniformScale = 1.0f;
		return t;
	}

	void Test_ExecuteUndo()
	{
		EntityTransform target = MakeT(0.0f);
		auto writer = [&](EntityId, const EntityTransform& t) { target = t; };
		const EntityId id{EntityKind::LayoutInstance, 3u};
		SetEntityTransformCommand cmd(id, MakeT(0.0f), MakeT(5.0f), writer);
		cmd.Execute();
		REQUIRE(target.position.x == 5.0f);
		cmd.Undo();
		REQUIRE(target.position.x == 0.0f);
	}

	void Test_MergeSameEntityOnly()
	{
		EntityTransform target = MakeT(0.0f);
		auto writer = [&](EntityId, const EntityTransform& t) { target = t; };
		const EntityId id{EntityKind::MeshInsert, 1u};
		SetEntityTransformCommand a(id, MakeT(0.0f), MakeT(1.0f), writer);
		SetEntityTransformCommand b(id, MakeT(1.0f), MakeT(2.0f), writer);
		REQUIRE(a.TryMerge(b));
		a.Execute();
		REQUIRE(target.position.x == 2.0f);      // 'new' absorbé
		a.Undo();
		REQUIRE(target.position.x == 0.0f);       // 'old' initial conservé

		// Une entité différente ne fusionne pas.
		SetEntityTransformCommand c(EntityId{EntityKind::MeshInsert, 2u}, MakeT(2.0f), MakeT(3.0f), writer);
		REQUIRE(!a.TryMerge(c));
	}

	void Test_CoalesceViaCommandStack()
	{
		EntityTransform target = MakeT(0.0f);
		auto writer = [&](EntityId, const EntityTransform& t) { target = t; };
		const EntityId id{EntityKind::LayoutInstance, 0u};
		CommandStack stack;

		stack.Push(std::make_unique<SetEntityTransformCommand>(id, MakeT(0.0f), MakeT(10.0f), writer));
		REQUIRE(target.position.x == 10.0f);
		REQUIRE(stack.UndoSize() == 1u);

		stack.Undo();
		REQUIRE(target.position.x == 0.0f);
		stack.Redo();
		REQUIRE(target.position.x == 10.0f);

		// Deux pushes consécutifs sur la même entité fusionnent -> 1 seul item.
		stack.Push(std::make_unique<SetEntityTransformCommand>(id, MakeT(10.0f), MakeT(20.0f), writer));
		stack.Push(std::make_unique<SetEntityTransformCommand>(id, MakeT(20.0f), MakeT(30.0f), writer));
		REQUIRE(target.position.x == 30.0f);
		REQUIRE(stack.UndoSize() == 1u);

		// Un seul Undo ramène à l'état d'avant tout le geste.
		stack.Undo();
		REQUIRE(target.position.x == 0.0f);
	}
}

int main()
{
	Test_ExecuteUndo();
	Test_MergeSameEntityOnly();
	Test_CoalesceViaCommandStack();

	if (g_failed == 0)
	{
		std::printf("[PASS] SetEntityTransformCommandTests\n");
		return 0;
	}
	std::printf("[FAIL] SetEntityTransformCommandTests: %d failure(s)\n", g_failed);
	return 1;
}
