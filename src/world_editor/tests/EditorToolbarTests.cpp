/// Tests unitaires CPU pour M100.35 — EditorToolbar.
///
/// Pas d'ImGui ni de GPU. On vérifie :
///   - Le layout calculé ne couvre PAS le viewport 3D (l'invariant clé du
///     ticket : la toolbar reste au-dessus, pas au-dessus du terrain).
///   - Le hit-test pur renvoie le bon index de bouton.
///   - `HandleClick` route correctement vers `WorldEditorShell::SetActiveTool`.
///   - Le fallback "icône absente" est non-crashable (l'atlas retourne un
///     style valide même pour le bouton de désélection / les outils non
///     mappés).
///   - `SetActiveTool` ne mute pas d'état "rendu" du shell (proxy : ne
///     touche pas à `m_dirty`, `m_initialized`, `m_panels.size()`).
///
/// Framework : REQUIRE maison + main monolithique (pattern identique aux
/// autres suites de tests world_editor).

#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/ui/EditorToolbar.h"
#include "src/world_editor/ui/ToolbarIconAtlas.h"

#include <cstdio>
#include <cstring>

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

	/// Test : la toolbar ne couvre PAS le viewport. La règle : la zone Y
	/// occupée par la toolbar reste sous la zone du viewport (toolbar.y +
	/// height ≤ topOfViewport). Concrètement : la toolbar est posée *juste
	/// au-dessus* du dockspace, dont le top commence où la toolbar finit.
	void Test_Toolbar_DoesNotCoverViewport()
	{
		WorldEditorShell shell;
		EditorToolbar toolbar(shell);

		// Viewport simulé : 1920x1080, menu bar 20 px.
		const float viewportWidth  = 1920.0f;
		const float menuBarHeight  =   20.0f;
		const ToolbarLayout layout = toolbar.BuildLayout(viewportWidth, menuBarHeight);

		REQUIRE(layout.toolbarHeight == EditorToolbar::kToolbarHeightPx);
		REQUIRE(layout.toolbarY == menuBarHeight);
		REQUIRE(layout.toolbarWidth == viewportWidth);

		// Le top du viewport 3D commence à `toolbarY + toolbarHeight`.
		const float viewportTopY = layout.toolbarY + layout.toolbarHeight;
		// Tous les boutons ont y < viewportTopY (donc strictement dans la
		// bande toolbar, jamais sous le terrain).
		for (const auto& b : layout.buttons)
		{
			REQUIRE(b.y + b.height <= viewportTopY);
		}
	}

	/// Test : click sur le bouton d'un outil donné route vers
	/// `SetActiveTool` avec le bon enum.
	void Test_Toolbar_ClickRoutesToSetActiveTool()
	{
		WorldEditorShell shell;
		EditorToolbar toolbar(shell);
		const ToolbarLayout layout = toolbar.BuildLayout(1920.0f, 20.0f);

		// Trouve le bouton Mountain Range et clique son centre.
		size_t mountainIdx = layout.buttons.size();
		for (size_t i = 0; i < layout.buttons.size(); ++i)
		{
			if (layout.buttons[i].tool == ActiveTool::MountainRange)
			{
				mountainIdx = i;
				break;
			}
		}
		REQUIRE(mountainIdx < layout.buttons.size());

		const auto& b = layout.buttons[mountainIdx];
		const float mx = b.x + b.width * 0.5f;
		const float my = b.y + b.height * 0.5f;

		size_t hitIdx = 0;
		REQUIRE(EditorToolbar::HitTest(layout, mx, my, hitIdx));
		REQUIRE(hitIdx == mountainIdx);

		REQUIRE(shell.GetActiveTool() == ActiveTool::None);
		toolbar.HandleClick(layout, hitIdx);
		REQUIRE(shell.GetActiveTool() == ActiveTool::MountainRange);

		// Idem pour Valley Chain.
		size_t valleyIdx = layout.buttons.size();
		for (size_t i = 0; i < layout.buttons.size(); ++i)
		{
			if (layout.buttons[i].tool == ActiveTool::ValleyChain)
			{
				valleyIdx = i;
				break;
			}
		}
		REQUIRE(valleyIdx < layout.buttons.size());
		toolbar.HandleClick(layout, valleyIdx);
		REQUIRE(shell.GetActiveTool() == ActiveTool::ValleyChain);

		// Cliquer le bouton X (désélection) bascule vers None.
		size_t deselectIdx = layout.buttons.size();
		for (size_t i = 0; i < layout.buttons.size(); ++i)
		{
			if (layout.buttons[i].tool == ActiveTool::None)
			{
				deselectIdx = i;
				break;
			}
		}
		REQUIRE(deselectIdx < layout.buttons.size());
		toolbar.HandleClick(layout, deselectIdx);
		REQUIRE(shell.GetActiveTool() == ActiveTool::None);
	}

	/// Test : HitTest hors zone de tous les boutons → false.
	void Test_Toolbar_HitTestOutsideAllButtons_ReturnsFalse()
	{
		WorldEditorShell shell;
		EditorToolbar toolbar(shell);
		const ToolbarLayout layout = toolbar.BuildLayout(1920.0f, 20.0f);

		size_t hitIdx = 999;
		// Bien sous tous les boutons (Y au milieu du viewport, où il n'y a
		// pas de toolbar).
		REQUIRE(!EditorToolbar::HitTest(layout, 100.0f, 500.0f, hitIdx));
		REQUIRE(hitIdx == 999); // pas muté

		// Au-dessus de toute la toolbar.
		REQUIRE(!EditorToolbar::HitTest(layout, 100.0f, 0.0f, hitIdx));
	}

	/// Test : l'atlas fournit un style placeholder valide pour chaque outil
	/// connu et un fallback pour l'inconnu — pas de crash, pas de pointeur
	/// null sur `letter` ou `tooltipFr`.
	void Test_Toolbar_MissingIcon_FallsBackToPlaceholder()
	{
		const ActiveTool kKnown[] = {
			ActiveTool::None,
			ActiveTool::TerrainSculpt,
			ActiveTool::TerrainStamp,
			ActiveTool::SplatPaint,
			ActiveTool::Lake,
			ActiveTool::River,
			ActiveTool::MountainRange,
			ActiveTool::ValleyChain,
		};
		for (ActiveTool t : kKnown)
		{
			const ToolIconStyle s = ToolbarIconAtlas::Get(t);
			REQUIRE(s.letter    != nullptr);
			REQUIRE(s.tooltipFr != nullptr);
			REQUIRE(std::strlen(s.letter)    > 0);
			REQUIRE(std::strlen(s.tooltipFr) > 0);
		}

		// Cast d'un uint8 hors enum → style par défaut "Bientôt disponible".
		const ToolIconStyle bogus = ToolbarIconAtlas::Get(static_cast<ActiveTool>(99));
		REQUIRE(bogus.enabled == false);
		REQUIRE(bogus.letter  != nullptr);

		const ToolIconStyle deselect = ToolbarIconAtlas::GetDeselect();
		REQUIRE(deselect.enabled == true);
		REQUIRE(std::strcmp(deselect.letter, "X") == 0);
	}

	/// Test : activation d'un nouvel outil ne déclenche AUCUN side effect
	/// sur l'état dirty / panels / initialized du shell. C'est un proxy
	/// observable pour l'invariant de visibilité du terrain : si
	/// `SetActiveTool` n'écrit que `m_activeTool`, alors par construction
	/// il ne peut pas muter `TerrainRenderer::IsFrustumCullEnabled`,
	/// `m_noUserTextures` ou la position caméra (qui vivent ailleurs).
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

		shell.SetActiveTool(ActiveTool::ValleyChain);
		REQUIRE(shell.GetActiveTool() == ActiveTool::ValleyChain);
		REQUIRE(shell.IsDirty() == dirtyBefore);

		shell.SetActiveTool(ActiveTool::None);
		REQUIRE(shell.GetActiveTool() == ActiveTool::None);
		REQUIRE(shell.IsDirty() == dirtyBefore);
	}
}

int main()
{
	Test_Toolbar_DoesNotCoverViewport();
	Test_Toolbar_ClickRoutesToSetActiveTool();
	Test_Toolbar_HitTestOutsideAllButtons_ReturnsFalse();
	Test_Toolbar_MissingIcon_FallsBackToPlaceholder();
	Test_NewToolActivation_DoesNotResetCameraOrFrustumCullState();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[EditorToolbarTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[EditorToolbarTests] all tests passed\n");
	return 0;
}
