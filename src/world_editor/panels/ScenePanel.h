#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/camera/EditorCameraController.h"

#include <cstdint>

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

		/// M100.34 incrément 1 — Texture ID ImGui (descriptor VkDescriptorSet
		/// reinterprété en uint64_t) de l'image offscreen `EditorViewportRenderTarget`
		/// owned par `Engine`. Renseigné depuis `Engine` chaque frame après
		/// Init de la target. 0 = pas encore disponible → fallback placeholder
		/// texte.
		///
		/// La PR 1 livre l'infra : le texture ID pointe sur une image **noire**
		/// (rien n'est encore copié dedans). La PR 2 branche la passe FrameGraph
		/// qui copie `SceneColor_LDR` dans cette image → le rendu réel apparaîtra
		/// dans le panneau.
		void     SetEditorViewportTextureId(uint64_t id) { m_editorViewportTexId = id; }
		uint64_t GetEditorViewportTextureId() const     { return m_editorViewportTexId; }

		/// M100.34 incr 4.1 — Rect écran (pixels client de la fenêtre OS) occupé
		/// par l'`ImGui::Image` du viewport offscreen lors de la dernière frame.
		/// Renseigné chaque frame dans `Render()` juste après `ImGui::Image`.
		/// Retourne `false` tant que le placeholder texte est affiché (texture
		/// non encore disponible) ou si le panel est replié/masqué — dans ce
		/// cas, le caller doit ignorer l'input éditeur (pas de pick, pas de
		/// brush preview).
		///
		/// Utilisé par `Engine.cpp` pour rebaser les coords souris du raycast
		/// terrain : sans ça les outils sculpt/splat/place tirent contre les
		/// dimensions du backbuffer plein écran alors que le rendu réel est
		/// confiné dans ce sous-rectangle (régression cassante depuis M100.34
		/// incrément 4 qui clear le swapchain en gris).
		bool GetViewportScreenRect(int& outMinX, int& outMinY,
			int& outMaxX, int& outMaxY) const
		{
			if (!m_hasViewportRect) return false;
			outMinX = m_contentMinX;
			outMinY = m_contentMinY;
			outMaxX = m_contentMaxX;
			outMaxY = m_contentMaxY;
			return true;
		}

	private:
		bool m_visible = true;
		int m_viewportWidth = 0;
		int m_viewportHeight = 0;
		engine::editor::world::EditorCameraController m_camera;
		uint64_t m_editorViewportTexId = 0u;

		// M100.34 incr 4.1 — capture du rect écran de l'`ImGui::Image` (rebase
		// des coords d'input par `Engine.cpp`). Reste à `false` tant qu'aucune
		// frame n'a affiché l'image (placeholder texte → input éditeur ignoré).
		bool m_hasViewportRect = false;
		int  m_contentMinX = 0;
		int  m_contentMinY = 0;
		int  m_contentMaxX = 0;
		int  m_contentMaxY = 0;
	};
}
