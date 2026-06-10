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

	void Test_MultiSelection()
	{
		EditorSelection sel;
		int calls = 0;
		sel.SetOnChanged([&](EntityId) { ++calls; });

		const EntityId a{EntityKind::LayoutInstance, 1};
		const EntityId b{EntityKind::LayoutInstance, 2};
		const EntityId c{EntityKind::MeshInsert, 0};

		// SelectMany : set = {a,b,c}, primaire = premier (a).
		sel.SelectMany({a, b, c});
		REQUIRE(calls == 1);
		REQUIRE(sel.SelectedSet().size() == 3);
		REQUIRE(sel.Current() == a);
		REQUIRE(sel.IsSelected(b));
		REQUIRE(sel.IsSelected(c));

		// Select mono : set = {b}, primaire = b.
		sel.Select(b);
		REQUIRE(sel.SelectedSet().size() == 1);
		REQUIRE(sel.Current() == b);
		REQUIRE(!sel.IsSelected(a));

		// Toggle : ajoute a -> {b,a}.
		sel.ToggleInSelection(a);
		REQUIRE(sel.SelectedSet().size() == 2);
		REQUIRE(sel.IsSelected(a));
		// Toggle : retire b ; primaire se reporte sur un restant (a).
		sel.ToggleInSelection(b);
		REQUIRE(sel.SelectedSet().size() == 1);
		REQUIRE(!sel.IsSelected(b));
		REQUIRE(sel.Current() == a);

		// Clear vide le set + le primaire.
		sel.Clear();
		REQUIRE(sel.SelectedSet().empty());
		REQUIRE(!sel.HasSelection());

		// SelectMany vide = Clear sémantique (set vide, primaire None).
		sel.SelectMany({a});
		sel.SelectMany({});
		REQUIRE(sel.SelectedSet().empty());
		REQUIRE(sel.Current().kind == EntityKind::None);
	}
}

int main()
{
	Test_SelectionNotifiesOnChange();
	Test_MultiSelection();

	if (g_failed == 0)
	{
		std::printf("[PASS] EditorSelectionTests\n");
		return 0;
	}
	std::printf("[FAIL] EditorSelectionTests: %d failure(s)\n", g_failed);
	return 1;
}
