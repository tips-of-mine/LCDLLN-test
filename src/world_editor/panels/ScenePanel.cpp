#include "src/world_editor/panels/ScenePanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Rôle : panneau Scene du Shell éditeur. Historiquement (M100.34) il créait
	/// une fenêtre flottante « Scene » affichant, via une render target offscreen,
	/// la vue 3D du monde en édition.
	///
	/// Retrait de la fenêtre (demande utilisateur) : ce dock flottant n'était
	/// qu'un DOUBLON de la vue 3D principale plein écran (rendu moteur direct).
	/// `Render()` ne crée donc plus AUCUNE fenêtre ImGui — le panneau « Scene »
	/// n'apparaît plus à l'écran ni dans le menu View (cf. WorldEditorShell et
	/// WorldEditorImGui qui le filtrent par nom).
	///
	/// L'objet est néanmoins CONSERVÉ comme `m_panels[0]` du Shell pour préserver,
	/// sans refonte risquée : l'ordre d'index figé des panneaux (Inspector=1,
	/// AssetBrowser=2, …), les casts `WorldEditorShell::GetScenePanel()` (raccourcis
	/// caméra Numpad 1/3/7) et le branchement Engine de `EditorViewportRenderTarget`.
	/// La caméra (`m_camera`) et la taille de viewport restent donc accessibles.
	///
	/// Effet de bord : aucun (ne crée plus de window, ne touche plus l'atlas ImGui).
	/// Doit être appelée en main thread depuis `WorldEditorShell::RenderFrame`, comme
	/// les autres panneaux. Pour réactiver le viewport in-dock, restaurer le bloc
	/// `ImGui::Begin("Scene") … ImGui::End()` d'origine (cf. historique git).
	void ScenePanel::Render()
	{
		// Volontairement vide : plus de fenêtre « Scene » (doublon de la vue
		// principale). `m_camera`, `m_viewportWidth/Height` et `m_editorViewportTexId`
		// restent valides pour les consommateurs externes (Engine, raccourcis), mais
		// ne sont plus rafraîchis ici puisque le dock n'existe plus.
	}
}
