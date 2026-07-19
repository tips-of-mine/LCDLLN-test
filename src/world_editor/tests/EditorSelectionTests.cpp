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

	/// Roadmap-6 — Multi-sélection : Toggle ajoute/retire, Current = dernière
	/// sélectionnée (primaire), Contains/Count/Items cohérents, Select remplace
	/// toute la sélection.
	void Test_MultiSelectionToggle()
	{
		EditorSelection sel;
		int calls = 0;
		EntityId lastPrimary{};
		sel.SetOnChanged([&](EntityId id) { ++calls; lastPrimary = id; });

		const EntityId a{EntityKind::LayoutInstance, 1};
		const EntityId b{EntityKind::LayoutInstance, 2};
		const EntityId c{EntityKind::MeshInsert, 0};

		sel.Select(a);
		REQUIRE(calls == 1);
		sel.Toggle(b); // ajout : b devient primaire
		REQUIRE(calls == 2);
		REQUIRE(sel.Count() == 2u);
		REQUIRE(sel.Current() == b);
		REQUIRE(lastPrimary == b);
		REQUIRE(sel.Contains(a) && sel.Contains(b) && !sel.Contains(c));

		sel.Toggle(c); // ajout : c primaire, 3 au total
		REQUIRE(sel.Count() == 3u);
		REQUIRE(sel.Current() == c);
		REQUIRE(sel.Items().size() == 3u);
		REQUIRE(sel.Items().front() == a);

		sel.Toggle(c); // retrait : la primaire redevient b
		REQUIRE(sel.Count() == 2u);
		REQUIRE(sel.Current() == b);
		REQUIRE(!sel.Contains(c));

		// Select remplace TOUTE la sélection par {c}.
		sel.Select(c);
		REQUIRE(sel.Count() == 1u);
		REQUIRE(sel.Current() == c);
		REQUIRE(!sel.Contains(a) && !sel.Contains(b));

		// Toggle d'un kind None : no-op sans notification.
		const int before = calls;
		sel.Toggle(EntityId{});
		REQUIRE(calls == before);

		// Select d'un kind None ≡ Clear.
		sel.Select(EntityId{});
		REQUIRE(!sel.HasSelection());
		REQUIRE(sel.Count() == 0u);
	}
}

int main()
{
	Test_SelectionNotifiesOnChange();
	Test_MultiSelectionToggle();

	if (g_failed == 0)
	{
		std::printf("[PASS] EditorSelectionTests\n");
		return 0;
	}
	std::printf("[FAIL] EditorSelectionTests: %d failure(s)\n", g_failed);
	return 1;
}
