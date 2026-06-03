/// Tests unitaires CPU pour EditorSelection (sous-projet 1, bloc B).
/// Vérifie : notification au changement effectif, pas de notification sur
/// re-sélection identique, Clear et son idempotence. Pur CPU (aucune
/// dépendance ImGui/Vulkan), tourne sous ctest Linux.

#include "src/world_editor/scene/EditorSelection.h"

#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using namespace engine::editor::scene;

	void Test_SelectionNotifiesOnChange()
	{
		EditorSelection sel;
		int calls = 0;
		EntityId last{};
		sel.SetOnChanged([&](EntityId id) { ++calls; last = id; });

		sel.Select(EntityId{EntityKind::MeshInsert, 7});
		REQUIRE(calls == 1);
		REQUIRE(last.kind == EntityKind::MeshInsert);
		REQUIRE(last.index == 7u);
		REQUIRE(sel.HasSelection());
		REQUIRE(sel.Current().index == 7u);

		// Re-sélection identique : aucune notification supplémentaire.
		sel.Select(EntityId{EntityKind::MeshInsert, 7});
		REQUIRE(calls == 1);

		// Sélection différente : notifie.
		sel.Select(EntityId{EntityKind::LayoutInstance, 2});
		REQUIRE(calls == 2);
		REQUIRE(sel.Current().kind == EntityKind::LayoutInstance);

		sel.Clear();
		REQUIRE(calls == 3);
		REQUIRE(!sel.HasSelection());
		REQUIRE(sel.Current().kind == EntityKind::None);

		// Clear redondant : aucune notification.
		sel.Clear();
		REQUIRE(calls == 3);
	}
}

int main()
{
	Test_SelectionNotifiesOnChange();

	if (g_failed == 0)
	{
		std::printf("[PASS] EditorSelectionTests\n");
		return 0;
	}
	std::printf("[FAIL] EditorSelectionTests: %d failure(s)\n", g_failed);
	return 1;
}
