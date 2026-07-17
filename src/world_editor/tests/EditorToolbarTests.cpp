/// Tests unitaires CPU pour la barre d'actions (réorganisation UI
/// 2026-07-17 — ex-toolbar d'outils M100.35).
///
/// Pas d'ImGui ni de GPU. On vérifie :
///   - Le layout calculé ne couvre PAS le viewport 3D (invariant M100.35
///     conservé : la barre reste dans sa bande, jamais sur le terrain).
///   - Un id d'action absent du registre ne produit PAS de bouton.
///   - Le hit-test pur renvoie le bon index de bouton.
///   - `HandleClick` exécute l'action du registre (stub qui flippe un bool)
///     et respecte le prédicat `enabled` (action grisée = no-op).
///   - Le fallback de `ToolbarIconAtlas::GetForAction` est non-crashable.
///   - `SetActiveTool` ne mute pas d'état « rendu » du shell (proxy pour
///     l'invariant de visibilité du terrain).
///   - Dirty-tracking `NoteSaved`/`IsDirtySinceSave` (barre de statut +
///     modale Quitter).
///
/// Framework : REQUIRE maison + main monolithique (pattern identique aux
/// autres suites de tests world_editor).

#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/ui/EditorToolbar.h"
#include "src/world_editor/ui/ToolbarIconAtlas.h"

#include <cstdio>
#include <cstring>
#include <functional>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::editor::world::ActiveTool;
	using engine::editor::world::EditorToolbar;
	using engine::editor::world::ToolbarLayout;
	using engine::editor::world::ToolbarIconAtlas;
	using engine::editor::world::ToolIconStyle;
	using engine::editor::world::WorldEditorShell;
	using engine::editor::world::actions::ActionCategory;
	using engine::editor::world::actions::EditorAction;

	/// Enregistre une action stub `id` qui incrémente `counter` à chaque
	/// exécution. `enabled` nul => toujours active.
	void RegisterStub(WorldEditorShell& shell, const char* id, int* counter,
		std::function<bool()> enabled = nullptr)
	{
		EditorAction a;
		a.id = id;
		a.label = id;
		a.category = ActionCategory::Fichier;
		a.enabled = std::move(enabled);
		a.execute = [counter] { ++(*counter); };
		(void)shell.MutableActionRegistry().Register(std::move(a));
	}

	/// Enregistre les 5 actions attendues par la barre, comptées dans
	/// `counters[0..4]` (ordre save/undo/redo/validate/export).
	void RegisterAllFive(WorldEditorShell& shell, int* counters)
	{
		RegisterStub(shell, "file.save",     &counters[0]);
		RegisterStub(shell, "edit.undo",     &counters[1]);
		RegisterStub(shell, "edit.redo",     &counters[2]);
		RegisterStub(shell, "zone.validate", &counters[3]);
		RegisterStub(shell, "zone.export",   &counters[4]);
	}

	/// Test : la barre ne couvre PAS le viewport (toolbar.y + height borne
	/// tous les boutons) et les 5 actions donnent 5 boutons.
	void Test_Toolbar_DoesNotCoverViewport()
	{
		WorldEditorShell shell;
		int counters[5] = {};
		RegisterAllFive(shell, counters);
		EditorToolbar toolbar(shell);

		// Viewport simulé : 1920x1080, menu bar 20 px.
		const float viewportWidth  = 1920.0f;
		const float menuBarHeight  =   20.0f;
		const ToolbarLayout layout = toolbar.BuildLayout(viewportWidth, menuBarHeight);

		REQUIRE(layout.toolbarHeight == EditorToolbar::kToolbarHeightPx);
		REQUIRE(layout.toolbarY == menuBarHeight);
		REQUIRE(layout.toolbarWidth == viewportWidth);
		REQUIRE(layout.buttons.size() == 5u);

		// Le top du viewport 3D commence à `toolbarY + toolbarHeight`.
		const float viewportTopY = layout.toolbarY + layout.toolbarHeight;
		for (const auto& b : layout.buttons)
		{
			REQUIRE(b.y + b.height <= viewportTopY);
		}
	}

	/// Test : un id absent du registre est omis du layout (mode shell
	/// in-game : les actions session ne sont pas enregistrées).
	void Test_Toolbar_UnknownActionOmitted()
	{
		WorldEditorShell shell;
		int c0 = 0, c1 = 0;
		RegisterStub(shell, "edit.undo", &c0);
		RegisterStub(shell, "edit.redo", &c1);
		EditorToolbar toolbar(shell);

		const ToolbarLayout layout = toolbar.BuildLayout(1920.0f, 20.0f);
		REQUIRE(layout.buttons.size() == 2u);
		REQUIRE(layout.buttons[0].actionId == "edit.undo");
		REQUIRE(layout.buttons[1].actionId == "edit.redo");

		// Registre entièrement vide → aucun bouton, pas de crash.
		WorldEditorShell emptyShell;
		EditorToolbar emptyToolbar(emptyShell);
		REQUIRE(emptyToolbar.BuildLayout(1920.0f, 20.0f).buttons.empty());
	}

	/// Test : click sur un bouton route vers l'exécution de la BONNE action.
	void Test_Toolbar_ClickExecutesAction()
	{
		WorldEditorShell shell;
		int counters[5] = {};
		RegisterAllFive(shell, counters);
		EditorToolbar toolbar(shell);
		const ToolbarLayout layout = toolbar.BuildLayout(1920.0f, 20.0f);

		// Trouve le bouton "zone.validate" et clique son centre.
		size_t validateIdx = layout.buttons.size();
		for (size_t i = 0; i < layout.buttons.size(); ++i)
		{
			if (layout.buttons[i].actionId == "zone.validate") { validateIdx = i; break; }
		}
		REQUIRE(validateIdx < layout.buttons.size());

		const auto& b = layout.buttons[validateIdx];
		size_t hitIdx = 0;
		REQUIRE(EditorToolbar::HitTest(layout,
			b.x + b.width * 0.5f, b.y + b.height * 0.5f, hitIdx));
		REQUIRE(hitIdx == validateIdx);

		toolbar.HandleClick(layout, hitIdx);
		REQUIRE(counters[3] == 1); // zone.validate exécutée
		REQUIRE(counters[0] == 0 && counters[1] == 0
			&& counters[2] == 0 && counters[4] == 0);

		// Index hors plage : no-op.
		toolbar.HandleClick(layout, layout.buttons.size());
		REQUIRE(counters[3] == 1);
	}

	/// Test : une action au prédicat `enabled` faux n'est PAS exécutée au
	/// clic (bouton grisé).
	void Test_Toolbar_DisabledActionIsNoOp()
	{
		WorldEditorShell shell;
		int execCount = 0;
		bool gate = false;
		RegisterStub(shell, "file.save", &execCount, [&gate] { return gate; });
		EditorToolbar toolbar(shell);
		const ToolbarLayout layout = toolbar.BuildLayout(1920.0f, 20.0f);
		REQUIRE(layout.buttons.size() == 1u);

		toolbar.HandleClick(layout, 0);
		REQUIRE(execCount == 0); // grisée → no-op

		gate = true;
		toolbar.HandleClick(layout, 0);
		REQUIRE(execCount == 1); // réactivée → exécutée
	}

	/// Test : HitTest hors zone de tous les boutons → false.
	void Test_Toolbar_HitTestOutsideAllButtons_ReturnsFalse()
	{
		WorldEditorShell shell;
		int counters[5] = {};
		RegisterAllFive(shell, counters);
		EditorToolbar toolbar(shell);
		const ToolbarLayout layout = toolbar.BuildLayout(1920.0f, 20.0f);

		size_t hitIdx = 999;
		// Bien sous tous les boutons (Y au milieu du viewport).
		REQUIRE(!EditorToolbar::HitTest(layout, 100.0f, 500.0f, hitIdx));
		REQUIRE(hitIdx == 999); // pas muté

		// Au-dessus de toute la barre.
		REQUIRE(!EditorToolbar::HitTest(layout, 100.0f, 0.0f, hitIdx));
	}

	/// Test : l'atlas fournit un style valide pour chaque action de la barre
	/// et un fallback pour l'inconnu — pas de pointeur nul.
	void Test_Toolbar_ActionIcons_FallBackToPlaceholder()
	{
		const char* kActionIds[] = {
			"file.save", "edit.undo", "edit.redo", "zone.validate", "zone.export",
		};
		for (const char* id : kActionIds)
		{
			const ToolIconStyle s = ToolbarIconAtlas::GetForAction(id);
			REQUIRE(s.enabled);
			REQUIRE(s.letter    != nullptr && std::strlen(s.letter)    > 0);
			REQUIRE(s.tooltipFr != nullptr && std::strlen(s.tooltipFr) > 0);
		}

		const ToolIconStyle bogus = ToolbarIconAtlas::GetForAction("absent.id");
		REQUIRE(bogus.enabled == false);
		REQUIRE(bogus.letter != nullptr);

		// L'atlas outils (consommé par la palette + la barre de statut)
		// reste non-crashable.
		const ToolIconStyle deselect = ToolbarIconAtlas::GetDeselect();
		REQUIRE(deselect.enabled == true);
		REQUIRE(std::strcmp(deselect.letter, "X") == 0);
	}

	/// Test (réorganisation UI 2026-07-17) : NoteSaved/IsDirtySinceSave — le
	/// dirty-tracking consommé par la barre de statut et la modale Quitter.
	void Test_Shell_DirtySinceSave_Tracking()
	{
		WorldEditorShell shell;
		REQUIRE(!shell.IsDirtySinceSave());

		shell.MarkDirty("test");
		REQUIRE(shell.IsDirtySinceSave());

		shell.NoteSaved();
		REQUIRE(!shell.IsDirtySinceSave());

		// Mutation de la pile (Clear incrémente le sériel) → dirty à nouveau.
		shell.MutableCommandStack().Clear();
		REQUIRE(shell.IsDirtySinceSave());

		shell.NoteSaved();
		REQUIRE(!shell.IsDirtySinceSave());
	}

	/// Test : activation d'un nouvel outil ne déclenche AUCUN side effect
	/// sur l'état dirty / panels / initialized du shell. Proxy observable de
	/// l'invariant de visibilité du terrain (cf. M100.35).
	void Test_NewToolActivation_DoesNotResetCameraOrFrustumCullState()
	{
		WorldEditorShell shell;
		const bool dirtyBefore = shell.IsDirty();
		const bool initBefore  = shell.IsInitialized();
		const size_t panelsBefore = shell.Panels().size();
		shell.SetActiveTool(ActiveTool::MountainRange);
		REQUIRE(shell.GetActiveTool() == ActiveTool::MountainRange);
		REQUIRE(shell.IsDirty() == dirtyBefore);
		REQUIRE(shell.IsInitialized() == initBefore);
		REQUIRE(shell.Panels().size() == panelsBefore);

		shell.SetActiveTool(ActiveTool::None);
		REQUIRE(shell.GetActiveTool() == ActiveTool::None);
		REQUIRE(shell.IsDirty() == dirtyBefore);
	}
}

int main()
{
	Test_Toolbar_DoesNotCoverViewport();
	Test_Toolbar_UnknownActionOmitted();
	Test_Toolbar_ClickExecutesAction();
	Test_Toolbar_DisabledActionIsNoOp();
	Test_Toolbar_HitTestOutsideAllButtons_ReturnsFalse();
	Test_Toolbar_ActionIcons_FallBackToPlaceholder();
	Test_Shell_DirtySinceSave_Tracking();
	Test_NewToolActivation_DoesNotResetCameraOrFrustumCullState();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[EditorToolbarTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[EditorToolbarTests] all tests passed\n");
	return 0;
}
