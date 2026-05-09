#pragma once
#include "src/world_editor/world/IPanel.h"
#include "src/world_editor/world/EditorCameraController.h"

namespace engine::editor::world::panels
{
	/// Panneau Scene du shell éditeur monde (M100.1 + M100.4).
	/// Contiendra la vue 3D principale du monde en édition à partir de
	/// M100.34 (intégration rendu offscreen). Détient depuis M100.4 un
	/// `EditorCameraController` qui pilote les trois modes FPS / Orbital /
	/// TopDownOrtho. La barre de mode `[FPS] [Orbital] [Top]` est rendue
	/// par ce panneau et reflète `m_camera.GetMode()`.
	///
	/// Les dimensions de viewport sont exposées pour permettre à
	/// `EditorCameraController` (M100.4) et au pipeline de rendu offscreen
	/// (M100.34) de redimensionner leurs ressources GPU.
	class ScenePanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Scene"; }

		/// Rend la barre de mode caméra, le HUD focus, puis le placeholder
		/// viewport. À M100.34 : intègre l'image rendue offscreen via
		/// ImGui::Image. Effet de bord : crée une window ImGui nommée
		/// "Scene" et met à jour les membres m_viewportWidth/Height.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Largeur courante de la zone client du panneau, en pixels.
		/// Mise à jour à chaque Render(). Vaut 0 avant la première frame.
		/// Consommé par M100.4 (EditorCameraController) et M100.34 (rendu).
		int GetViewportWidth() const { return m_viewportWidth; }

		/// Hauteur courante de la zone client du panneau, en pixels.
		int GetViewportHeight() const { return m_viewportHeight; }

		/// Accès mutable au contrôleur caméra du panneau (M100.4). Utilisé
		/// par `WorldEditorShell::HandleShortcut` pour brancher les Numpad
		/// 1/3/7 sur `SetMode`, et par les outils qui appellent `FocusOn`.
		engine::editor::world::EditorCameraController& MutableCamera() { return m_camera; }

		/// Accès lecture seule au contrôleur caméra (M100.4). Utilisé par
		/// le rendu offscreen (M100.34) qui appelle `BuildCamera` chaque
		/// frame pour obtenir la matrice de vue/projection courante.
		const engine::editor::world::EditorCameraController& GetCamera() const { return m_camera; }

	private:
		bool m_visible = true;
		int m_viewportWidth = 0;
		int m_viewportHeight = 0;
		engine::editor::world::EditorCameraController m_camera;
	};
}
